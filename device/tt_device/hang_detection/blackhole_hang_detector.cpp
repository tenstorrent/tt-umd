/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/blackhole_hang_detector.hpp"

#include <vector>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

BlackholeHangDetector::BlackholeHangDetector(
    DeviceProtocol* protocol, architecture_implementation* arch_impl, bool noc_translation_enabled) :
    HangDetector(protocol, arch_impl), noc_translation_enabled_(noc_translation_enabled) {}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_bar() {
    return get_pcie_interface()->bar_read32(get_arch_impl()->get_read_checking_offset());
}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_noc(NocId noc) {
    tt_xy_pair core = get_hang_check_core(noc);
    uint64_t addr = get_arch_impl()->get_noc_reg_base(CoreType::PCIE, static_cast<uint32_t>(noc)) +
                    get_arch_impl()->get_noc_node_id_offset();
    return read_noc_reg_via_probe_window(core, addr, noc);
}

tt_xy_pair BlackholeHangDetector::get_hang_check_core(NocId noc) const {
    if (noc != NocId::NOC1) {
        return blackhole::PCIE_CORES_TYPE2_NOC0[0];
    }
    uint32_t noc1_x = blackhole::NOC0_X_TO_NOC1_X[blackhole::PCIE_CORES_TYPE2_NOC0[0].x];
    // NOC1 y-coordinate depends on whether NOC translation is enabled.
    uint32_t noc1_y = noc_translation_enabled_ ? 0 : blackhole::NOC0_Y_TO_NOC1_Y[blackhole::PCIE_CORES_TYPE2_NOC0[0].y];
    return tt_xy_pair(noc1_x, noc1_y);
}

}  // namespace tt::umd
