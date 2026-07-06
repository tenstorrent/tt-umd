// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "device/api/umd/device/warm_reset.hpp"
#include "device/api/umd/device/warm_reset_with_recovery.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/mmio_timeout_config.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::utils;

class HangDetectionTest : public ::testing::Test {
protected:
    static constexpr uint64_t WH_NOC_HANG_ADDR = 0xFFB11030;
    static constexpr uint64_t BH_NOC_HANG_ADDR = 0xFFBA0000;

    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<SocDescriptor> soc_desc_;

    void SetUp() override {
        if (utils::is_arm_platform()) {
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
        soc_desc_ = std::make_unique<SocDescriptor>(tt_device_->get_soc_descriptor());
    }

    // Deliberately hangs the specified NOC by reading an address that causes the NOC transaction to
    // never complete. On WH, this targets a TDMA register on the tensix core. On BH, the tensix core
    // is first put into reset, then a read to a private tensix address is issued. Both approaches were
    // empirically found by the tt-exalens team to reliably hang the NOC. Returns nothing: the read's
    // value is meaningless here (and with the per-op MMIO timeout active the read aborts before it can
    // return one) — the NOC is hung regardless. Callers verify the hang via is_noc_hung() / the timeout.
    void hang_noc(tt_xy_pair tensix_core, NocId noc = NocId::NOC0) {
        uint32_t hang_read_value = 0;
        if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
            tt_device_->set_risc_reset_state(
                tensix_core,
                tt_device_->get_architecture_implementation()->get_soft_reset_reg_value(RiscType::ALL_TENSIX));
        }
        NocIdSwitcher switcher(noc);
        try {
            tt_device_->read_from_device(
                &hang_read_value, tensix_core, noc_hang_addr(tt_device_->get_arch()), sizeof(hang_read_value));
        } catch (const error::UmdException<error::DeviceTimeoutError>&) {
            // Once the per-op MMIO timeout is active, the hang-inducing read stalls past the
            // default 100 ms budget and aborts before returning. The NOC is hung regardless.
        }
    }

    void warm_reset_and_reinit() {
        int pci_device_id = tt_device_->get_pci_device()->get_device_num();
        tt_device_.reset();
        soc_desc_.reset();
        WarmResetWithRecovery::warm_reset();

        auto cluster = test_utils::make_default_test_cluster();
        EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after warm reset.";
        cluster.reset();

        init_device(pci_device_id);
    }

protected:
    uint64_t noc_hang_addr(tt::ARCH arch) {
        switch (arch) {
            case tt::ARCH::WORMHOLE_B0:
                return WH_NOC_HANG_ADDR;
            case tt::ARCH::BLACKHOLE:
                return BH_NOC_HANG_ADDR;
            default:
                UMD_THROW(error::RuntimeError, fmt::format("Invalid architecture: {}.", arch));
        }
    }
};

// TODO: the NOC hang detection itself is verified, but the post-reset warm-reset + topology
// rediscovery path is still occasionally flaky and needs further investigation.

class NocHangDetectionTest : public HangDetectionTest, public ::testing::WithParamInterface<NocId> {
protected:
    static tt_xy_pair extract_node_id(uint32_t reg_val) { return tt_xy_pair(reg_val & 0x3F, (reg_val >> 6) & 0x3F); }

    // Returns the BAR0 address for the NOC node ID register for the given arch and NOC.
    uint32_t get_bar_node_id_offset(tt::ARCH arch, NocId noc) {
        if (arch == tt::ARCH::WORMHOLE_B0) {
            // WH: ARC NIU BAR0 base + node ID register offset (0x2C).
            uint32_t niu_base =
                (noc == NocId::NOC0) ? wormhole::NIU_CFG_NOC0_BAR_ARC_ADDR : wormhole::NIU_CFG_NOC1_BAR_ARC_ADDR;
            return niu_base + wormhole::NOC_NODE_ID_OFFSET;
        } else if (arch == tt::ARCH::BLACKHOLE) {
            // BH: PCIe NIU BAR0 base + node ID register offset (0x44).
            uint32_t niu_base =
                (noc == NocId::NOC0) ? blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR : blackhole::NIU_CFG_NOC1_BAR_PCIE_ADDR;
            return niu_base + blackhole::NOC_NODE_ID_OFFSET;
        }
        UMD_THROW(error::RuntimeError, "Unsupported architecture.");
    }
};

// TODO: Add reading NODE ID via NOC.
TEST_P(NocHangDetectionTest, ReadNodeIdViaBar) {
    NocId noc = GetParam();
    tt::ARCH arch = tt_device_->get_arch();

    uint32_t bar_raw = tt_device_->bar_read32(get_bar_node_id_offset(arch, noc));
    tt_xy_pair bar_node_id = extract_node_id(bar_raw);

    log_info(
        LogUMD, "NOC{} node ID: BAR=({},{}) [0x{:08X}]", static_cast<int>(noc), bar_node_id.x, bar_node_id.y, bar_raw);

    EXPECT_NE(bar_raw, 0xFFFFFFFF) << "BAR read returned all ones.";

    EXPECT_FALSE(tt_device_->is_noc_hung(noc, TTDevice::HangAction::RETURN))
        << "NOC" << static_cast<int>(noc) << " appears hung on a healthy device.";
}

TEST_P(NocHangDetectionTest, TestIsNocHungAPI) {
    NocId noc_to_hang = GetParam();

    if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE && noc_to_hang == NocId::NOC0) {
        GTEST_SKIP()
            << "BH: Hanging NOC0 on BH can prevent warm reset from working and a host reboot is then necessary.";
    }

    ASSERT_FALSE(tt_device_->is_noc_hung(noc_to_hang, TTDevice::HangAction::RETURN))
        << "is_noc_hung() returned true before any hang.";

    tt_xy_pair tensix_core = soc_desc_->get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    hang_noc(tensix_core, noc_to_hang);

    EXPECT_TRUE(tt_device_->is_noc_hung(noc_to_hang, TTDevice::HangAction::RETURN))
        << "is_noc_hung() did not detect the hang.";

    warm_reset_and_reinit();

    EXPECT_FALSE(tt_device_->is_noc_hung(noc_to_hang, TTDevice::HangAction::RETURN))
        << "is_noc_hung() still true after warm reset.";
}

// Verifies the per-op MMIO timeout (PR #2629) on the memcpy read path and that it is runtime
// configurable via MmioTimeoutConfig: once a NOC is hung its reads complete only after ~700 ms, so a
// budget below that aborts the memcpy with DeviceTimeoutError (instead of letting ops pile up and grind
// the host), while a budget above it lets the same read complete. Exercises two budgets to show usage.
TEST_P(NocHangDetectionTest, PerOpTimeoutThrowsOnHungNoc) {
    NocId noc_to_hang = GetParam();

    if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE && noc_to_hang == NocId::NOC0) {
        GTEST_SKIP()
            << "BH: Hanging NOC0 on BH can prevent warm reset from working and a host reboot is then necessary.";
    }

    tt_xy_pair tensix_core = soc_desc_->get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    hang_noc(tensix_core, noc_to_hang);

    const auto original_budget = MmioTimeoutConfig::get_op_timeout();
    uint32_t value = 0;
    NocIdSwitcher switcher(noc_to_hang);

    // Budget 1: tight (below the ~700 ms hung-read latency) — the memcpy read trips the per-op timeout.
    MmioTimeoutConfig::set_op_timeout(std::chrono::milliseconds(100));
    EXPECT_THROW(
        tt_device_->read_from_device(&value, tensix_core, noc_hang_addr(tt_device_->get_arch()), sizeof(value)),
        error::UmdException<error::DeviceTimeoutError>)
        << "A read on the hung NOC should trip the 100 ms per-op budget.";

    // Budget 2: generous (above the hung-read latency) — the same read completes without tripping.
    MmioTimeoutConfig::set_op_timeout(std::chrono::milliseconds(5000));
    EXPECT_NO_THROW(
        tt_device_->read_from_device(&value, tensix_core, noc_hang_addr(tt_device_->get_arch()), sizeof(value)))
        << "A 5 s per-op budget should outlast the hung-read latency.";

    // Restore the process-global default so the change doesn't leak into other tests.
    MmioTimeoutConfig::set_op_timeout(original_budget);

    warm_reset_and_reinit();
}

INSTANTIATE_TEST_SUITE_P(
    PerNoc,
    NocHangDetectionTest,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return (info.param == NocId::NOC0) ? "NOC0" : "NOC1"; });

// Hangs NOC0 on the first discovered device, then runs topology discovery with device init
// failures ignored, and verifies that the resulting cluster descriptor records a NocHangError
// in its health errors for the unhealthy device.
TEST_F(HangDetectionTest, TopologyDiscoveryRecordsNocHangHealthError) {
    if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP()
            << "BH: Hanging NOC0 on BH can prevent warm reset from working and a host reboot is then necessary.";
    }

    int pci_device_id = tt_device_->get_pci_device()->get_device_num();

    // NOC0 is the NOC that init_tt_device() probes during discovery, so hang that one.
    tt_xy_pair tensix_core = soc_desc_->get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    hang_noc(tensix_core, NocId::NOC0);
    ASSERT_TRUE(tt_device_->is_noc_hung(NocId::NOC0, TTDevice::HangAction::RETURN))
        << "Failed to hang NOC0 before running topology discovery.";

    // Release the device so topology discovery can open it fresh and hit the hang during init.
    tt_device_.reset();
    soc_desc_.reset();

    TopologyDiscoveryOptions options;
    options.discover_remote_devices = false;
    options.device_init_failure_action = TopologyDiscoveryOptions::Action::IGNORE;
    auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    // The hung device should be discovered as unhealthy and have a NocHangError recorded.
    EXPECT_FALSE(cluster_desc->get_unhealthy_devices().empty()) << "No unhealthy devices were recorded.";

    bool found_noc_hang = false;
    for (const auto& [chip_id, errors] : cluster_desc->get_health_errors()) {
        for (const auto& error : errors) {
            if (std::holds_alternative<error::NocHangError>(error)) {
                found_noc_hang = true;
            }
        }
    }
    EXPECT_TRUE(found_noc_hang) << "Expected a NocHangError in cluster descriptor health errors.";

    // Release discovery's device handles, then warm reset to recover the hung device so the host
    // and subsequent tests are left in a good state.
    cluster_desc.reset();
    devices.clear();
    WarmResetWithRecovery::warm_reset();
    init_device(pci_device_id);
}
