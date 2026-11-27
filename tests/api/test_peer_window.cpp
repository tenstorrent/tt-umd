// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "tt-umd-workload/chip/chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/protocol/peer_dma_interface.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// Host-page-aligned for any host page size up to 64 KiB, and SAFE_IO_L1_ADDRESS lies inside [0, RANGE_SIZE).
constexpr size_t RANGE_SIZE = 0x20000;

std::unique_ptr<TTDevice> open_device(int pci_device_id) {
    std::unique_ptr<TTDevice> device = create_tt_device(pci_device_id);
    device->set_power_state(TTDevice::PowerState::BUSY);
    device->init_tt_device();
    return device;
}

CoreCoord first_core(TTDevice& device, CoreType core_type) {
    return device.get_soc_descriptor().get_cores(core_type, CoordSystem::TRANSLATED)[0];
}

}  // namespace

// Peer-to-peer access needs two different Blackhole devices on one host.
class ApiPeerWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        pci_device_ids_ = PCIDevice::enumerate_devices();
        if (pci_device_ids_.size() < 2) {
            GTEST_SKIP() << "A peer window needs two devices.";
        }
        initiator_ = open_device(pci_device_ids_[0]);
        target_ = open_device(pci_device_ids_[1]);
        if (initiator_->get_arch() != ARCH::BLACKHOLE || target_->get_arch() != ARCH::BLACKHOLE) {
            GTEST_SKIP() << "Peer windows are supported only on Blackhole.";
        }
    }

    void TearDown() override {
        for (TTDevice* device : {initiator_.get(), target_.get()}) {
            if (device != nullptr) {
                device->set_power_state(TTDevice::PowerState::IDLE);
            }
        }
    }

    int initiator_id() const { return initiator_->get_communication_device_id(); }

    // Blackhole's PCIe core passes NOC addresses below 2^58 to PCIe unchanged, so the host can drive the initiator's
    // peer traffic itself, through the initiator's own PCIe core, without running anything on either chip.
    // window maps [range_start, ...) of target_core; addr is a target address inside it.
    void expect_write_through_initiator_lands(
        const PeerWindow& window, CoreCoord target_core, uint64_t range_start, uint64_t addr, uint32_t pattern) {
        // Clear the target first, so a pattern left by an earlier run cannot pass the test.
        const uint32_t zero = 0;
        target_->write_to_device(&zero, target_core, addr, sizeof(zero));
        uint32_t cleared = 0xffffffff;
        target_->read_from_device(&cleared, target_core, addr, sizeof(cleared));
        ASSERT_EQ(cleared, 0);

        const CoreCoord initiator_pcie = first_core(*initiator_, CoreType::PCIE);
        const uint64_t peer_addr = window.get_address() + (addr - range_start);
        initiator_->write_to_device(&pattern, initiator_pcie, peer_addr, sizeof(pattern));

        // The read follows the write along the same path, so it returns only once the write has landed.
        uint32_t read_through_initiator = 0;
        initiator_->read_from_device(
            &read_through_initiator, initiator_pcie, peer_addr, sizeof(read_through_initiator));
        EXPECT_EQ(read_through_initiator, pattern);

        uint32_t read_on_target = 0;
        target_->read_from_device(&read_on_target, target_core, addr, sizeof(read_on_target));
        EXPECT_EQ(read_on_target, pattern);
    }

    std::vector<int> pci_device_ids_;
    std::unique_ptr<TTDevice> initiator_;
    std::unique_ptr<TTDevice> target_;
};

class ApiPeerWindowNocTest : public ApiPeerWindowTest, public ::testing::WithParamInterface<NocId> {};

TEST_P(ApiPeerWindowNocTest, WriteThroughInitiatorReachesTargetCore) {
    const CoreCoord target_core = first_core(*target_, CoreType::TENSIX);
    const std::unique_ptr<PeerWindow> window =
        target_->export_window_to_device(initiator_id(), target_core, 0, RANGE_SIZE, IoOrdering::STRICT, GetParam());

    expect_write_through_initiator_lands(*window, target_core, 0, SAFE_IO_L1_ADDRESS, 0x600d600d);
}

INSTANTIATE_TEST_SUITE_P(
    ApiPeerWindowTest,
    ApiPeerWindowNocTest,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return info.param == NocId::NOC0 ? "NOC0" : "NOC1"; });

// An accepted DRAM range and the target word inside it that the initiator writes.
struct MappedRange {
    std::string name;
    uint64_t addr;
    size_t size;
    uint64_t word;
};

class ApiPeerWindowMappedRangeTest : public ApiPeerWindowTest, public ::testing::WithParamInterface<MappedRange> {};

TEST_P(ApiPeerWindowMappedRangeTest, WriteThroughInitiatorReachesTargetDram) {
    const CoreCoord dram_core = first_core(*target_, CoreType::DRAM);
    const std::unique_ptr<PeerWindow> window = target_->export_window_to_device(
        initiator_id(), dram_core, GetParam().addr, GetParam().size, IoOrdering::STRICT);

    expect_write_through_initiator_lands(*window, dram_core, GetParam().addr, GetParam().word, 0x5eed5eed);
}

INSTANTIATE_TEST_SUITE_P(
    ApiPeerWindowTest,
    ApiPeerWindowMappedRangeTest,
    ::testing::Values(
        // The largest accepted range, one whole 2 MiB-aligned 2 MiB block, written at its last word.
        MappedRange{"WholeTwoMiBBlock", 0x200000, 0x200000, 0x3ffffc},
        // A range starting inside its 2 MiB block: get_address() is addr itself, not the block's start.
        MappedRange{"StartsInsideTwoMiBBlock", 0x3f0000, 0x10000, 0x3f0000}),
    [](const ::testing::TestParamInfo<MappedRange>& info) { return info.param.name; });

TEST_F(ApiPeerWindowTest, WindowOutlivesBothDevices) {
    const CoreCoord target_core = first_core(*target_, CoreType::TENSIX);
    const std::unique_ptr<PeerWindow> window =
        target_->export_window_to_device(initiator_id(), target_core, 0, RANGE_SIZE, IoOrdering::STRICT);

    // Close both devices and use the window through fresh handles to the same devices.
    initiator_->set_power_state(TTDevice::PowerState::IDLE);
    target_->set_power_state(TTDevice::PowerState::IDLE);
    initiator_.reset();
    target_.reset();
    initiator_ = open_device(pci_device_ids_[0]);
    target_ = open_device(pci_device_ids_[1]);

    expect_write_through_initiator_lands(*window, target_core, 0, SAFE_IO_L1_ADDRESS, 0x0badf00d);
}

// More windows than a Blackhole device has (202 of 2 MiB), each destroyed before the next: all succeed only if
// destruction gives the target's window back.
TEST_F(ApiPeerWindowTest, DestroyReleasesWindow) {
    const CoreCoord target_core = first_core(*target_, CoreType::TENSIX);
    for (int i = 0; i < 256; i++) {
        ASSERT_NO_THROW(target_->export_window_to_device(initiator_id(), target_core, 0, RANGE_SIZE)) << "window " << i;
    }
}

struct RejectedRange {
    std::string name;
    uint64_t addr;
    size_t size;
};

class ApiPeerWindowRejectedRangeTest : public ApiPeerWindowTest, public ::testing::WithParamInterface<RejectedRange> {};

TEST_P(ApiPeerWindowRejectedRangeTest, Throws) {
    EXPECT_THROW(
        target_->export_window_to_device(
            initiator_id(), first_core(*target_, CoreType::DRAM), GetParam().addr, GetParam().size),
        error::UmdException<error::RuntimeError>);
}

// Aligned values are multiples of 64 KiB, so they hold whatever the host page size is.
INSTANTIATE_TEST_SUITE_P(
    ApiPeerWindowTest,
    ApiPeerWindowRejectedRangeTest,
    ::testing::Values(
        RejectedRange{"UnalignedAddr", 0x10, 0x10000},
        RejectedRange{"UnalignedSize", 0, 0x10010},
        RejectedRange{"ZeroSize", 0, 0},
        RejectedRange{"CrossesTwoMiBBoundary", 0x1f0000, 0x20000},
        RejectedRange{"LargerThanTwoMiB", 0, 0x400000},
        RejectedRange{"EightGiB", 0, 0x200000000ULL}),
    [](const ::testing::TestParamInfo<RejectedRange>& info) { return info.param.name; });

// Rejections that need only the target device.
class ApiPeerWindowSingleDeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No Tenstorrent PCI devices found.";
        }
        target_ = open_device(pci_device_ids[0]);
        if (target_->get_arch() != ARCH::BLACKHOLE) {
            GTEST_SKIP() << "Peer windows are supported only on Blackhole.";
        }
    }

    void TearDown() override {
        if (target_ != nullptr) {
            target_->set_power_state(TTDevice::PowerState::IDLE);
        }
    }

    std::unique_ptr<TTDevice> target_;
};

TEST_F(ApiPeerWindowSingleDeviceTest, OwnDeviceIdThrows) {
    EXPECT_THROW(
        target_->export_window_to_device(
            target_->get_communication_device_id(), first_core(*target_, CoreType::TENSIX), 0, RANGE_SIZE),
        error::UmdException<error::RuntimeError>);
}

TEST_F(ApiPeerWindowSingleDeviceTest, MissingDeviceIdThrows) {
    EXPECT_THROW(
        target_->export_window_to_device(999, first_core(*target_, CoreType::TENSIX), 0, RANGE_SIZE),
        error::UmdException<error::RuntimeError>);
}

// Two devices of an architecture without peer windows, so the error is not the same-device one.
TEST(ApiPeerWindowUnsupportedArchTest, NonBlackholeThrows) {
    const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.size() < 2) {
        GTEST_SKIP() << "Needs two devices.";
    }
    std::unique_ptr<TTDevice> target = open_device(pci_device_ids[0]);
    if (target->get_arch() == ARCH::BLACKHOLE) {
        target->set_power_state(TTDevice::PowerState::IDLE);
        GTEST_SKIP() << "Peer windows are supported on Blackhole.";
    }

    EXPECT_THROW(
        target->export_window_to_device(pci_device_ids[1], first_core(*target, CoreType::TENSIX), 0, RANGE_SIZE),
        error::UmdException<error::RuntimeError>);
    target->set_power_state(TTDevice::PowerState::IDLE);
}

// A remote device reaches the host only through its gateway chip, so it has no peer-to-peer PCIe path of its own.
TEST(ApiPeerWindowRemoteDeviceTest, RemoteDeviceThrows) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No remote chips in the cluster.";
    }

    for (ChipId chip_id : cluster->get_target_remote_device_ids()) {
        TTDevice* remote_device = cluster->get_chip(chip_id)->get_tt_device();
        EXPECT_THROW(
            remote_device->export_window_to_device(
                remote_device->get_communication_device_id(),
                first_core(*remote_device, CoreType::TENSIX),
                0,
                RANGE_SIZE),
            error::UmdException<error::RuntimeError>)
            << "chip " << chip_id;
    }
}
