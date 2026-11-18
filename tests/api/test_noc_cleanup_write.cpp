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
    uint32_t value = 0;
    tt_device_->write_to_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
    {
        NocCleanupWrite cleanup_write = make_cleanup_write(0, 0xc1ea4);
        tt_device_->read_from_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
        EXPECT_EQ(value, 0);
    }
    tt_device_->read_from_device(&value, core_, SAFE_IO_L1_ADDRESS, sizeof(value));
    EXPECT_EQ(value, 0xc1ea4);
}

TEST_F(ApiNocCleanupWriteTest, PinReturnsDmaAddress) {
    if (!tt_device_->get_pci_device()->is_iommu_enabled()) {
        GTEST_SKIP() << "NocCleanupWrite::pin() needs an IOMMU.";
    }
    const size_t page_size = getpagesize();
    std::unique_ptr<void, decltype(&std::free)> buffer(std::aligned_alloc(page_size, page_size), &std::free);

    NocCleanupWrite cleanup_write = make_cleanup_write(0, 0);
    EXPECT_NE(cleanup_write.pin(buffer.get(), page_size), 0);
}

TEST_F(ApiNocCleanupWriteTest, InvalidNocThrows) {
    EXPECT_THROW(make_cleanup_write(2, 0), error::UmdException<error::RuntimeError>);
}
