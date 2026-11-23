// SPDX-FileCopyrightText: © 2026 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "tests/test_utils/test_api_common.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/kmd_versions.hpp"
#include "tt-umd/utils/noc_cleanup_write.hpp"

using namespace tt;
using namespace tt::umd;

class ApiNocCleanupWriteTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No Tenstorrent PCI devices found.";
        }
        if (PCIDevice::read_kmd_version() < KMD_NOC_CLEANUP) {
            GTEST_SKIP() << "KMD version " << PCIDevice::read_kmd_version().str() << " is below required "
                         << KMD_NOC_CLEANUP.str();
        }
        pci_device_id_ = pci_device_ids[0];
        tt_device_ = create_tt_device(pci_device_id_);
        tt_device_->set_power_state(TTDevice::PowerState::BUSY);
        tt_device_->init_tt_device();
        core_ = tt_device_->get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    }

    NocCleanupWrite make_cleanup_write(uint8_t noc, uint32_t data) {
        return NocCleanupWrite(
            pci_device_id_,
            static_cast<uint8_t>(core_.x),
            static_cast<uint8_t>(core_.y),
            noc,
            SAFE_IO_L1_ADDRESS,
            data);
    }

    int pci_device_id_ = 0;
    std::unique_ptr<TTDevice> tt_device_;
    CoreCoord core_;
};

TEST_F(ApiNocCleanupWriteTest, WritesWhenDestroyed) {
    for (uint8_t noc : {0, 1}) {
        uint32_t value = 0;
        tt_device_->write_to_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
        {
            NocCleanupWrite cleanup_write = make_cleanup_write(noc, 0xc1ea4);
            tt_device_->read_from_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
            EXPECT_EQ(value, 0) << "NOC " << int{noc};
        }
        tt_device_->read_from_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
        EXPECT_EQ(value, 0xc1ea4) << "NOC " << int{noc};
    }
}

TEST_F(ApiNocCleanupWriteTest, WritesAfterTheClientDropsItsPowerVote) {
    uint32_t value = 0;
    tt_device_->write_to_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
    {
        NocCleanupWrite cleanup_write = make_cleanup_write(0, 0xc1ea4);
        tt_device_.reset();
    }
    tt_device_ = create_tt_device(pci_device_id_);
    tt_device_->set_power_state(TTDevice::PowerState::BUSY);
    tt_device_->init_tt_device();
    tt_device_->read_from_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
    EXPECT_EQ(value, 0xc1ea4);
}

TEST_F(ApiNocCleanupWriteTest, PinReturnsDmaAddress) {
    const size_t page_size = getpagesize();
    std::unique_ptr<void, decltype(&std::free)> buffer(std::aligned_alloc(page_size, page_size), &std::free);

    NocCleanupWrite cleanup_write = make_cleanup_write(0, 0);
    EXPECT_NE(cleanup_write.pin(buffer.get(), page_size), 0);
}

TEST_F(ApiNocCleanupWriteTest, RefusedPinThrows) {
    const size_t page_size = getpagesize();
    std::unique_ptr<void, decltype(&std::free)> buffer(std::aligned_alloc(page_size, 2 * page_size), &std::free);

    NocCleanupWrite cleanup_write = make_cleanup_write(0, 0);
    EXPECT_THROW(cleanup_write.pin(buffer.get(), 0), error::UmdException<error::RuntimeError>);
    EXPECT_THROW(
        cleanup_write.pin(static_cast<char*>(buffer.get()) + 4, page_size), error::UmdException<error::RuntimeError>);
}

TEST_F(ApiNocCleanupWriteTest, MissingDeviceThrows) {
    EXPECT_THROW(
        NocCleanupWrite(255, static_cast<uint8_t>(core_.x), static_cast<uint8_t>(core_.y), 0, SAFE_IO_L1_ADDRESS, 0),
        error::UmdException<error::RuntimeError>);
}

TEST_F(ApiNocCleanupWriteTest, InvalidNocThrows) {
    EXPECT_THROW(make_cleanup_write(2, 0), error::UmdException<error::RuntimeError>);
}
