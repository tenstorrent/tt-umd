// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/hang_detection/blackhole_hang_detector.hpp"

#include "tt-umd/arch/architecture_registers.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"

namespace tt::umd {

namespace {

// NIU_CFG_0 of an NIU, and the bit that turns coordinate translation on for its NOC.
constexpr uint32_t NIU_CFG_0_OFFSET = 0x100;
constexpr uint32_t NIU_CFG_0_NOC_ID_TRANSLATE_EN = 1u << 14;

// Base of the ARC tile's NIU block for each NOC, as seen from the NOC.
constexpr uint64_t ARC_NIU_NOC0_BASE = blackhole::NIU_CFG_NOC0_ARC_ADDR - NIU_CFG_0_OFFSET;
constexpr uint64_t ARC_NIU_NOC1_BASE = blackhole::NIU_CFG_NOC1_ARC_ADDR - NIU_CFG_0_OFFSET;

}  // namespace

BlackholeHangDetector::BlackholeHangDetector(DeviceProtocol* device_protocol, PcieInterface* pcie_interface) :
    HangDetectorImplementation(device_protocol, pcie_interface) {}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_bar() {
    // The PCIe tile's own NOC node id is served from BAR0 without crossing the NOC, so only a dead link reads all
    // ones.
    return pcie_interface_->bar_read32(get_architecture_registers(tt::ARCH::BLACKHOLE).noc_node_id_bar_offset);
}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_noc(NocId noc) {
    // The ARC tile's node id register for the probed NOC never reads all ones while the NOC delivers. Where ARC sits on
    // NOC1 depends on translation, which the PCIe tile's NIU reports over BAR0 without using the NOC.
    const uint32_t niu_bar_base =
        noc == NocId::NOC1 ? blackhole::NIU_CFG_NOC1_BAR_PCIE_ADDR : blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR;
    const bool translation_enabled =
        (pcie_interface_->bar_read32(niu_bar_base + NIU_CFG_0_OFFSET) & NIU_CFG_0_NOC_ID_TRANSLATE_EN) != 0;
    const tt_xy_pair arc_core = blackhole::get_arc_core(translation_enabled, noc == NocId::NOC1);
    const uint64_t arc_niu_base = noc == NocId::NOC1 ? ARC_NIU_NOC1_BASE : ARC_NIU_NOC0_BASE;
    return read_noc_register(arc_core, arc_niu_base + blackhole::NOC_NODE_ID_OFFSET, noc);
}

}  // namespace tt::umd
