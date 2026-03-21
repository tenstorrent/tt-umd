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

    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<SocDescriptor> soc_desc_;

    void SetUp() override {
        if (is_arm_platform()) {
            GTEST_SKIP() << "Skipping on ARM64 – NOC hang can lock up the system.";
        }
        std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No PCI devices found.";
        }
        init_device(pci_device_ids.at(0));
    }

    void init_device(int pci_device_id) {
        tt_device_ = TTDevice::create(pci_device_id);
        tt_device_->init_tt_device();
        soc_desc_ = std::make_unique<SocDescriptor>(tt_device_->get_arch(), tt_device_->get_chip_info());
    }

    uint32_t read_hang_check_reg_via_bar() {
        return tt_device_->bar_read32(tt_device_->get_architecture_implementation()->get_read_checking_offset());
    }

    uint32_t read_hang_check_reg_via_noc(NocId noc = NocId::NOC0) {
        const auto* arch_impl = tt_device_->get_architecture_implementation();
        uint32_t value = 0;

        if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
            tt_xy_pair arc_core = soc_desc_->get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];
            uint64_t scratch6_noc_addr =
                arch_impl->get_arc_apb_noc_base_address() + arch_impl->get_arc_reset_scratch_offset() + 6 * 4;

            NocIdSwitcher noc_switcher(noc);
            tt_device_->read_from_device(&value, arc_core, scratch6_noc_addr, sizeof(value));
        } else {
            tt_xy_pair pcie_core = soc_desc_->get_cores(CoreType::PCIE, CoordSystem::TRANSLATED)[0];
            uint64_t noc_node_id_addr = arch_impl->get_noc_reg_base(CoreType::PCIE, static_cast<uint32_t>(noc)) +
                                        arch_impl->get_noc_node_id_offset();

            NocIdSwitcher noc_switcher(noc);
            tt_device_->read_from_device(&value, pcie_core, noc_node_id_addr, sizeof(value));
        }

        return value;
    }

    uint32_t hang_noc(tt_xy_pair tensix_core, NocId noc = NocId::NOC0) {
        uint32_t hang_read_value = 0;
        if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
            NocIdSwitcher switcher(noc);
            tt_device_->read_from_device(&hang_read_value, tensix_core, WH_NOC_HANG_ADDR, sizeof(hang_read_value));
        } else if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
            tt_device_->set_risc_reset_state(tensix_core, static_cast<uint32_t>(TENSIX_ASSERT_SOFT_RESET));
            NocIdSwitcher switcher(noc);
            tt_device_->read_from_device(&hang_read_value, tensix_core, BH_NOC_HANG_ADDR, sizeof(hang_read_value));
        }
        return hang_read_value;
    }

    void warm_reset_and_reinit() {
        int pci_device_id = tt_device_->get_pci_device()->get_device_num();
        tt_device_.reset();
        soc_desc_.reset();
        WarmReset::warm_reset();

        auto cluster = std::make_unique<Cluster>();
        EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after warm reset.";
        cluster.reset();

        init_device(pci_device_id);
    }
};

TEST_F(HangDetectionTest, HangCheckRegisterReadEquivalence) {
    uint32_t bar_value = read_hang_check_reg_via_bar();
    uint32_t noc_value = read_hang_check_reg_via_noc(NocId::NOC0);

    log_info(LogUMD, "hang-check BAR=0x{:08X}  NOC=0x{:08X}", bar_value, noc_value);

    EXPECT_NE(bar_value, 0xFFFFFFFF) << "BAR read returned all ones.";
    EXPECT_NE(noc_value, 0xFFFFFFFF) << "NOC read returned all ones.";
    EXPECT_EQ(bar_value, noc_value) << "BAR and NOC reads differ.";
}

class NocHangDetectionTest : public HangDetectionTest, public ::testing::WithParamInterface<NocId> {};

TEST_P(NocHangDetectionTest, TestNocHangDetection) {
    NocId noc_to_hang = GetParam();
    NocId verify_noc = (noc_to_hang == NocId::NOC0) ? NocId::NOC1 : NocId::NOC0;

    if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE && noc_to_hang == NocId::NOC0) {
        GTEST_SKIP()
            << "BH: Hanging NOC0 on BH can prevent warm reset from working and a host reboot is then necessary.";
    }

    tt_xy_pair tensix_core = soc_desc_->get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    uint32_t baseline = read_hang_check_reg_via_noc(verify_noc);
    ASSERT_NE(baseline, 0xFFFFFFFF) << "NOC" << static_cast<int>(verify_noc) << " appears hung before test started.";

    uint32_t hang_read_value = hang_noc(tensix_core, noc_to_hang);

    uint32_t verify_value = read_hang_check_reg_via_noc(verify_noc);

    log_info(
        LogUMD,
        "After hanging NOC{}: hang_read=0x{:08X}, verify NOC{}=0x{:08X}, baseline=0x{:08X}",
        static_cast<int>(noc_to_hang),
        hang_read_value,
        static_cast<int>(verify_noc),
        verify_value,
        baseline);

    EXPECT_NE(verify_value, 0xFFFFFFFF) << "NOC" << static_cast<int>(verify_noc)
                                        << " should still work after hanging NOC" << static_cast<int>(noc_to_hang);
    EXPECT_EQ(verify_value, baseline) << "NOC" << static_cast<int>(verify_noc)
                                      << " value should match pre-hang baseline.";

    warm_reset_and_reinit();

    uint32_t bar_recovered = read_hang_check_reg_via_bar();
    EXPECT_NE(bar_recovered, 0xFFFFFFFF) << "BAR read still returns all ones after warm reset.";

    uint32_t noc_recovered = read_hang_check_reg_via_noc(verify_noc);
    EXPECT_NE(noc_recovered, 0xFFFFFFFF) << "NOC read still returns all ones after warm reset.";

    log_info(LogUMD, "Post-reset: BAR=0x{:08X}  NOC=0x{:08X}", bar_recovered, noc_recovered);
}

INSTANTIATE_TEST_SUITE_P(
    HangNoc,
    NocHangDetectionTest,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return (info.param == NocId::NOC0) ? "NOC0" : "NOC1"; });

TEST_F(HangDetectionTest, DISABLED_TestDeviceHangDetection) {
    ASSERT_FALSE(tt_device_->is_hardware_hung()) << "is_hardware_hung() returned true before any hang.";

    // TODO: Add hang_pcie_tile() to make BAR reads return 0xFFFFFFFF.

    EXPECT_TRUE(tt_device_->is_hardware_hung()) << "is_hardware_hung() did not detect the hang.";

    warm_reset_and_reinit();

    EXPECT_FALSE(tt_device_->is_hardware_hung()) << "is_hardware_hung() still true after warm reset.";
}
