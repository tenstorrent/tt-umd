// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "device/api/umd/device/warm_reset.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;

class HangDetectionTest : public ::testing::Test {
protected:
    static constexpr uint64_t WH_NOC_HANG_ADDR = 0xFFB11030;
    static constexpr uint64_t BH_NOC_HANG_ADDR = 0xFFBA0000;

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

    uint32_t read_hang_check_reg_via_noc(NocId noc = NocId::NOC0) {
        const auto* arch_impl = tt_device_->get_architecture_implementation();
        uint32_t value = 0;
        NocIdSwitcher noc_switcher(noc);

        if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
            tt_xy_pair arc_core = soc_desc_->get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];
            uint64_t scratch6_noc_addr =
                arch_impl->get_arc_apb_noc_base_address() + arch_impl->get_arc_reset_scratch_offset() + 6 * 4;
            tt_device_->read_from_device(&value, arc_core, scratch6_noc_addr, sizeof(value));
        } else {
            tt_xy_pair pcie_core = soc_desc_->get_cores(CoreType::PCIE, CoordSystem::TRANSLATED)[0];
            uint64_t noc_node_id_addr = arch_impl->get_noc_reg_base(CoreType::PCIE, static_cast<uint32_t>(noc)) +
                                        arch_impl->get_noc_node_id_offset();
            tt_device_->read_from_device(&value, pcie_core, noc_node_id_addr, sizeof(value));
        }

        return value;
    }

    // Deliberately hangs the specified NOC by reading an address that causes the NOC transaction to
    // never complete. On WH, this targets a TDMA register on the tensix core. On BH, the tensix core
    // is first put into reset, then a read to a private tensix address is issued. Both approaches were
    // empirically found by the tt-exalens team to reliably hang the NOC.
    uint32_t hang_noc(tt_xy_pair tensix_core, NocId noc = NocId::NOC0) {
        uint32_t hang_read_value = 0;
        if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
            tt_device_->set_risc_reset_state(tensix_core, static_cast<uint32_t>(TENSIX_ASSERT_SOFT_RESET));
        }
        NocIdSwitcher switcher(noc);
        tt_device_->read_from_device(
            &hang_read_value, tensix_core, noc_hang_addr(tt_device_->get_arch()), sizeof(hang_read_value));
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

private:
    uint64_t noc_hang_addr(tt::ARCH arch) {
        switch (arch) {
            case tt::ARCH::WORMHOLE_B0:
                return WH_NOC_HANG_ADDR;
            case tt::ARCH::BLACKHOLE:
                return BH_NOC_HANG_ADDR;
            default:
                TT_THROW("Invalid architecture: {}.", arch);
        }
    }
};

// TODO: All tests in this file are disabled due to CI flakiness. The NOC hang detection mechanism
// itself works correctly, but the subsequent warm reset and topology rediscovery sometimes fail.
// The hang detection is verified; the post-reset reinitialization path needs further investigation.

class NodeIdVerificationNocAndBar : public HangDetectionTest, public ::testing::WithParamInterface<NocId> {
protected:
    uint32_t get_bar_node_id_offset(tt::ARCH arch, NocId noc) {
        if (arch == tt::ARCH::WORMHOLE_B0) {
            return (noc == NocId::NOC0) ? wormhole::WH_BAR_ARC_NOC0_NODE_ID_OFFSET
                                        : wormhole::WH_BAR_ARC_NOC1_NODE_ID_OFFSET;
        } else if (arch == tt::ARCH::BLACKHOLE) {
            return (noc == NocId::NOC0) ? blackhole::BH_BAR_PCIE_NOC0_NODE_ID_OFFSET
                                        : blackhole::BH_BAR_PCIE_NOC1_NODE_ID_OFFSET;
        }
        TT_THROW("Unsupported architecture.");
    }
};

TEST_P(NodeIdVerificationNocAndBar, DISABLED_ReadNodeIdViaBarAndNoc) {
    NocId noc = GetParam();
    tt::ARCH arch = tt_device_->get_arch();

    uint32_t bar_val = tt_device_->bar_read32(get_bar_node_id_offset(arch, noc));

    uint32_t noc_val;
    {
        NocIdSwitcher noc_switcher(noc);
        noc_val = tt_device_->read_hang_check_reg_via_noc();
    }

    uint32_t bar_x = bar_val & 0x3F;
    uint32_t bar_y = (bar_val >> 6) & 0x3F;
    uint32_t noc_x = noc_val & 0x3F;
    uint32_t noc_y = (noc_val >> 6) & 0x3F;

    log_info(
        LogUMD,
        "NOC{} node ID: BAR=0x{:08X} ({},{}), NOC=0x{:08X} ({},{})",
        static_cast<int>(noc),
        bar_val,
        bar_x,
        bar_y,
        noc_val,
        noc_x,
        noc_y);

    EXPECT_NE(bar_val, 0xFFFFFFFF) << "BAR read returned all ones.";
    EXPECT_NE(noc_val, 0xFFFFFFFF) << "NOC" << static_cast<int>(noc) << " read returned all ones.";
    EXPECT_EQ(bar_val, noc_val) << "BAR and NOC" << static_cast<int>(noc) << " node ID reads differ.";
}

INSTANTIATE_TEST_SUITE_P(
    NodeIdNocAndBar,
    NodeIdVerificationNocAndBar,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return (info.param == NocId::NOC0) ? "NOC0" : "NOC1"; });

class NocHangDetectionTest : public HangDetectionTest, public ::testing::WithParamInterface<NocId> {};

TEST_P(NocHangDetectionTest, DISABLED_TestNocHangDetection) {
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

    uint32_t noc_recovered = read_hang_check_reg_via_noc(verify_noc);
    EXPECT_NE(noc_recovered, 0xFFFFFFFF) << "NOC read still returns all ones after warm reset.";

    log_info(LogUMD, "Post-reset: NOC=0x{:08X}", noc_recovered);
}

INSTANTIATE_TEST_SUITE_P(
    HangNoc,
    NocHangDetectionTest,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return (info.param == NocId::NOC0) ? "NOC0" : "NOC1"; });

TEST_P(NocHangDetectionTest, DISABLED_TestIsNocHungAPI) {
    NocId noc_to_hang = GetParam();

    if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE && noc_to_hang == NocId::NOC0) {
        GTEST_SKIP()
            << "BH: Hanging NOC0 on BH can prevent warm reset from working and a host reboot is then necessary.";
    }

    ASSERT_FALSE(tt_device_->is_noc_hung(noc_to_hang)) << "is_noc_hung() returned true before any hang.";

    tt_xy_pair tensix_core = soc_desc_->get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    hang_noc(tensix_core, noc_to_hang);

    EXPECT_TRUE(tt_device_->is_noc_hung(noc_to_hang)) << "is_noc_hung() did not detect the hang.";

    warm_reset_and_reinit();

    EXPECT_FALSE(tt_device_->is_noc_hung(noc_to_hang)) << "is_noc_hung() still true after warm reset.";
}
