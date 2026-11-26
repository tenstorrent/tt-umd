// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tests/test_utils/test_api_common.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/peer_window.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

std::unique_ptr<TTDevice> open_device(int pci_device_id) {
    std::unique_ptr<TTDevice> device = create_tt_device(pci_device_id);
    device->set_power_state(TTDevice::PowerState::BUSY);
    device->init_tt_device();
    return device;
}

CoreCoord first_tensix(TTDevice& device) {
    return device.get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
}

CoreCoord first_pcie(TTDevice& device) {
    return device.get_soc_descriptor().get_cores(CoreType::PCIE, CoordSystem::TRANSLATED)[0];
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

    std::vector<int> pci_device_ids_;
    std::unique_ptr<TTDevice> initiator_;
    std::unique_ptr<TTDevice> target_;
};

TEST_F(ApiPeerWindowTest, AddressIsPageAligned) {
    const PeerWindow window(*initiator_, *target_, first_tensix(*target_), 0, getpagesize());

    EXPECT_NE(window.get_address(), 0);
    EXPECT_EQ(window.get_address() % getpagesize(), 0);
}

// Blackhole's PCIe core passes NOC addresses below 2^58 to PCIe unchanged, so the host can drive the initiator's
// peer traffic itself, through the initiator's own PCIe core, without running anything on either chip.
// Two pages, so SAFE_IO_L1_ADDRESS lies inside the mapped range whatever the host page size is.
TEST_F(ApiPeerWindowTest, WriteThroughInitiatorReachesTargetCore) {
    const CoreCoord target_core = first_tensix(*target_);
    const uint32_t zero = 0;
    target_->write_to_device(&zero, target_core, SAFE_IO_L1_ADDRESS, sizeof(zero));

    const PeerWindow window(*initiator_, *target_, target_core, 0, 2 * getpagesize(), IoOrdering::STRICT);
    const uint32_t pattern = 0x600d600d;
    initiator_->write_to_device(
        &pattern, first_pcie(*initiator_), window.get_address() + SAFE_IO_L1_ADDRESS, sizeof(pattern));

    // The read follows the write along the same path, so it returns only once the write has landed.
    uint32_t read_through_initiator = 0;
    initiator_->read_from_device(
        &read_through_initiator, first_pcie(*initiator_), window.get_address() + SAFE_IO_L1_ADDRESS, sizeof(pattern));
    EXPECT_EQ(read_through_initiator, 0x600d600d);

    uint32_t read_on_target = 0;
    target_->read_from_device(&read_on_target, target_core, SAFE_IO_L1_ADDRESS, sizeof(read_on_target));
    EXPECT_EQ(read_on_target, 0x600d600d);
}

// Larger than a 2 MiB window, so the range needs one of the target's larger windows.
TEST_F(ApiPeerWindowTest, RangeLargerThan2MiBIsMapped) {
    const CoreCoord dram_core = target_->get_soc_descriptor().get_cores(CoreType::DRAM, CoordSystem::TRANSLATED)[0];
    const uint32_t zero = 0;
    target_->write_to_device(&zero, dram_core, 0x200000, sizeof(zero));

    const PeerWindow window(*initiator_, *target_, dram_core, 0, 0x400000, IoOrdering::STRICT);
    const uint32_t pattern = 0x600d600d;
    initiator_->write_to_device(&pattern, first_pcie(*initiator_), window.get_address() + 0x200000, sizeof(pattern));
    uint32_t read_through_initiator = 0;
    initiator_->read_from_device(
        &read_through_initiator, first_pcie(*initiator_), window.get_address() + 0x200000, sizeof(pattern));
    EXPECT_EQ(read_through_initiator, 0x600d600d);

    uint32_t read_on_target = 0;
    target_->read_from_device(&read_on_target, dram_core, 0x200000, sizeof(read_on_target));
    EXPECT_EQ(read_on_target, 0x600d600d);
}

TEST_F(ApiPeerWindowTest, WindowOutlivesBothDevices) {
    const CoreCoord target_core = first_tensix(*target_);
    const PeerWindow window(*initiator_, *target_, target_core, 0, 2 * getpagesize(), IoOrdering::STRICT);

    // Close both devices and use the window through fresh handles to the same devices.
    initiator_->set_power_state(TTDevice::PowerState::IDLE);
    target_->set_power_state(TTDevice::PowerState::IDLE);
    initiator_.reset();
    target_.reset();
    initiator_ = open_device(pci_device_ids_[0]);
    target_ = open_device(pci_device_ids_[1]);
    const uint32_t zero = 0;
    target_->write_to_device(&zero, target_core, SAFE_IO_L1_ADDRESS, sizeof(zero));

    const uint32_t pattern = 0x0badf00d;
    initiator_->write_to_device(
        &pattern, first_pcie(*initiator_), window.get_address() + SAFE_IO_L1_ADDRESS, sizeof(pattern));
    uint32_t read_through_initiator = 0;
    initiator_->read_from_device(
        &read_through_initiator, first_pcie(*initiator_), window.get_address() + SAFE_IO_L1_ADDRESS, sizeof(pattern));
    EXPECT_EQ(read_through_initiator, 0x0badf00d);

    uint32_t read_on_target = 0;
    target_->read_from_device(&read_on_target, target_core, SAFE_IO_L1_ADDRESS, sizeof(read_on_target));
    EXPECT_EQ(read_on_target, 0x0badf00d);
}

// More windows than a Blackhole device has, each destroyed before the next: all succeed only if destruction
// gives the target's window back.
TEST_F(ApiPeerWindowTest, DestroyReleasesWindow) {
    const CoreCoord target_core = first_tensix(*target_);
    for (int i = 0; i < 256; i++) {
        ASSERT_NO_THROW(PeerWindow(*initiator_, *target_, target_core, 0, getpagesize())) << "window " << i;
    }
}

TEST_F(ApiPeerWindowTest, RangeLargerThanAnyWindowThrows) {
    EXPECT_THROW(
        PeerWindow(*initiator_, *target_, first_tensix(*target_), 0, 0x200000000ULL),
        error::UmdException<error::RuntimeError>);
}

// Offsets in host pages, so the cases hold whatever the host page size is.
struct RejectedRange {
    uint64_t addr_bytes;
    size_t size_pages;
    size_t size_extra_bytes;
};

std::string rejected_range_name(const ::testing::TestParamInfo<RejectedRange>& info) {
    const std::vector<std::string> names = {"UnalignedAddr", "UnalignedSize", "ZeroSize"};
    return names[info.index];
}

class ApiPeerWindowRejectedRangeTest : public ApiPeerWindowTest, public ::testing::WithParamInterface<RejectedRange> {};

TEST_P(ApiPeerWindowRejectedRangeTest, Throws) {
    const RejectedRange range = GetParam();
    const size_t size = range.size_pages * getpagesize() + range.size_extra_bytes;

    EXPECT_THROW(
        PeerWindow(*initiator_, *target_, first_tensix(*target_), range.addr_bytes, size),
        error::UmdException<error::RuntimeError>);
}

INSTANTIATE_TEST_SUITE_P(
    ApiPeerWindowTest,
    ApiPeerWindowRejectedRangeTest,
    ::testing::Values(
        RejectedRange{/*addr_bytes=*/0x10, /*size_pages=*/1, /*size_extra_bytes=*/0},
        RejectedRange{/*addr_bytes=*/0, /*size_pages=*/1, /*size_extra_bytes=*/0x10},
        RejectedRange{/*addr_bytes=*/0, /*size_pages=*/0, /*size_extra_bytes=*/0}),
    rejected_range_name);

TEST(ApiPeerWindowSingleDeviceTest, SameDeviceThrows) {
    const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    std::unique_ptr<TTDevice> device = open_device(pci_device_ids[0]);
    if (device->get_arch() != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer windows are supported only on Blackhole.";
    }

    EXPECT_THROW(
        PeerWindow(*device, *device, first_tensix(*device), 0, getpagesize()),
        error::UmdException<error::RuntimeError>);
    device->set_power_state(TTDevice::PowerState::IDLE);
}

TEST(ApiPeerWindowSingleDeviceTest, NonBlackholeThrows) {
    const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.size() < 2) {
        GTEST_SKIP() << "Needs two devices, so the error is not the same-device one.";
    }
    std::unique_ptr<TTDevice> initiator = open_device(pci_device_ids[0]);
    std::unique_ptr<TTDevice> target = open_device(pci_device_ids[1]);
    if (initiator->get_arch() == ARCH::BLACKHOLE && target->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer windows are supported on Blackhole.";
    }

    EXPECT_THROW(
        PeerWindow(*initiator, *target, first_tensix(*target), 0, getpagesize()),
        error::UmdException<error::RuntimeError>);
}
