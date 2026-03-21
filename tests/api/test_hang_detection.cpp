// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "device/api/umd/device/warm_reset.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;

class HangDetectionTest : public ::testing::Test {
protected:
    static constexpr uint64_t WH_NOC_HANG_ADDR = 0xFFB11030;
    static constexpr uint64_t BH_NOC_HANG_ADDR = 0xFFB14000;

    std::vector<int> pci_device_ids_;
    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<SocDescriptor> soc_desc_;

    void SetUp() override {
        pci_device_ids_ = PCIDevice::enumerate_devices();
        if (pci_device_ids_.empty()) {
            GTEST_SKIP() << "No PCI devices found.";
        }
    }

    void init_device(int pci_device_id = -1) {
        if (pci_device_id < 0) {
            pci_device_id = pci_device_ids_.at(0);
        }
        tt_device_ = TTDevice::create(pci_device_id);
        tt_device_->init_tt_device();
        soc_desc_ = std::make_unique<SocDescriptor>(tt_device_->get_arch(), tt_device_->get_chip_info());
    }

    uint32_t read_hang_check_via_bar() {
        return tt_device_->bar_read32(tt_device_->get_architecture_implementation()->get_read_checking_offset());
    }

    uint32_t read_hang_check_via_noc(uint32_t noc_index = 0) {
        const auto* arch_impl = tt_device_->get_architecture_implementation();
        uint32_t value = 0;

        if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
            tt_xy_pair arc_core = soc_desc_->get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];
            uint64_t scratch6_noc_addr =
                arch_impl->get_arc_apb_noc_base_address() + arch_impl->get_arc_reset_scratch_offset() + 6 * 4;

            NocIdSwitcher noc_switcher(static_cast<NocId>(noc_index));
            tt_device_->read_from_device(&value, arc_core, scratch6_noc_addr, sizeof(value));
        } else {
            tt_xy_pair pcie_core = soc_desc_->get_cores(CoreType::PCIE, CoordSystem::TRANSLATED)[0];
            uint64_t noc_node_id_addr =
                arch_impl->get_noc_reg_base(CoreType::PCIE, noc_index) + arch_impl->get_noc_node_id_offset();

            NocIdSwitcher noc_switcher(static_cast<NocId>(noc_index));
            tt_device_->read_from_device(&value, pcie_core, noc_node_id_addr, sizeof(value));
        }

        return value;
    }

    void hang_noc(tt_xy_pair tensix_core, NocId noc = NocId::NOC0) {
        if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
            uint32_t dummy;
            NocIdSwitcher switcher(noc);
            tt_device_->read_from_device(&dummy, tensix_core, WH_NOC_HANG_ADDR, sizeof(dummy));
        } else if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
            tt_device_->set_risc_reset_state(tensix_core, static_cast<uint32_t>(TENSIX_ASSERT_SOFT_RESET));
            uint32_t dummy;
            NocIdSwitcher switcher(noc);
            tt_device_->read_from_device(&dummy, tensix_core, BH_NOC_HANG_ADDR, sizeof(dummy));
        }
    }

    void warm_reset_and_reinit() {
        tt_device_.reset();
        soc_desc_.reset();
        WarmReset::warm_reset();

        auto cluster = std::make_unique<Cluster>();
        EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after warm reset.";
        cluster.reset();

        init_device();
    }
};

TEST_F(HangDetectionTest, HangCheckRegisterReadEquivalence) {
    for (int pci_device_id : pci_device_ids_) {
        init_device(pci_device_id);

        uint32_t bar_value = read_hang_check_via_bar();
        uint32_t noc_value = read_hang_check_via_noc(0);

        log_info(LogUMD, "Device {}: hang-check BAR=0x{:08X}  NOC=0x{:08X}", pci_device_id, bar_value, noc_value);

        EXPECT_NE(bar_value, 0xFFFFFFFF) << "BAR read returned all ones on device " << pci_device_id;
        EXPECT_NE(noc_value, 0xFFFFFFFF) << "NOC read returned all ones on device " << pci_device_id;
        EXPECT_EQ(bar_value, noc_value) << "BAR and NOC reads differ on device " << pci_device_id;
    }
}

TEST_F(HangDetectionTest, TestNocHangDetection) {
    if (is_arm_platform()) {
        GTEST_SKIP() << "Skipping on ARM64 – NOC hang can lock up the system.";
    }

    init_device();
    tt_xy_pair tensix_core = soc_desc_->get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    uint32_t bar_baseline = read_hang_check_via_bar();
    ASSERT_NE(bar_baseline, 0xFFFFFFFF) << "Hardware already hung before test started.";

    uint32_t noc0_baseline = read_hang_check_via_noc(0);
    ASSERT_NE(noc0_baseline, 0xFFFFFFFF) << "NOC0 appears hung before test started.";

    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        log_info(
            LogUMD,
            "WH: Intentionally hanging NOCs by reading 0x{:08X} on tensix core ({}, {})",
            WH_NOC_HANG_ADDR,
            tensix_core.x,
            tensix_core.y);

        hang_noc(tensix_core);

        uint32_t bar_hung = read_hang_check_via_bar();
        EXPECT_EQ(bar_hung, 0xFFFFFFFF) << "Expected BAR read to return all ones after hanging NOC on WH.";

        log_info(LogUMD, "WH: Performing warm reset to recover from NOC hang.");

    } else if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
        log_info(
            LogUMD,
            "BH: Hanging NOC1 by reading 0x{:08X} via NOC1 on soft-reset tensix ({}, {}).",
            BH_NOC_HANG_ADDR,
            tensix_core.x,
            tensix_core.y);

        hang_noc(tensix_core, NocId::NOC1);

        uint32_t noc0_value = read_hang_check_via_noc(static_cast<uint8_t>(NocId::NOC0));
        log_info(LogUMD, "BH: NOC0 read after NOC1 hang: 0x{:08X}", noc0_value);
        EXPECT_NE(noc0_value, 0xFFFFFFFF) << "NOC0 should still be functional after hanging NOC1 on BH.";
        EXPECT_EQ(noc0_value, noc0_baseline) << "NOC0 read should match the pre-hang baseline value.";

    } else {
        GTEST_SKIP() << "Unsupported architecture for NOC hang test.";
    }

    warm_reset_and_reinit();
    uint32_t bar_recovered = read_hang_check_via_bar();
    EXPECT_NE(bar_recovered, 0xFFFFFFFF) << "BAR read still returns all ones after warm reset.";

    uint32_t noc_recovered = read_hang_check_via_noc(0);
    EXPECT_NE(noc_recovered, 0xFFFFFFFF) << "NOC read still returns all ones after warm reset.";

    log_info(LogUMD, "Post-reset: BAR=0x{:08X}  NOC=0x{:08X}", bar_recovered, noc_recovered);
}

TEST_F(HangDetectionTest, DISABLED_TestDeviceHangDetection) {
    if (is_arm_platform()) {
        GTEST_SKIP() << "Skipping on ARM64 – NOC hang can lock up the system.";
    }

    init_device();
    tt_xy_pair tensix_core = soc_desc_->get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    ASSERT_FALSE(tt_device_->is_hardware_hung()) << "is_hardware_hung() returned true before any hang.";

    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        log_info(LogUMD, "WH: Hanging NOC via read of 0x{:08X}.", WH_NOC_HANG_ADDR);
        hang_noc(tensix_core);
    } else if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
        log_info(LogUMD, "BH: Soft-resetting tensix, then hanging NOC0 via 0x{:08X}.", BH_NOC_HANG_ADDR);
        hang_noc(tensix_core);
    } else {
        GTEST_SKIP() << "Unsupported architecture for device hang test.";
    }

    EXPECT_TRUE(tt_device_->is_hardware_hung()) << "is_hardware_hung() did not detect the hang.";
    EXPECT_THROW(tt_device_->detect_hang_read(), std::runtime_error);

    log_info(LogUMD, "Performing warm reset to recover.");
    warm_reset_and_reinit();

    EXPECT_FALSE(tt_device_->is_hardware_hung()) << "is_hardware_hung() still true after warm reset.";
}
