/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/wormhole_hang_detector.hpp"

#include "noc_access.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"

namespace tt::umd {

WormholeHangDetector::WormholeHangDetector(
    DeviceProtocol* protocol, architecture_implementation* arch_impl, tt_xy_pair arc_core) :
    HangDetector(protocol, arch_impl, arc_core) {}

uint32_t WormholeHangDetector::read_hang_check_reg_via_bar() {
    return get_pcie_interface()->bar_read32(get_arch_impl()->get_read_checking_offset());
}

uint32_t WormholeHangDetector::read_hang_check_reg_via_noc(NocId noc) {
    uint64_t addr = get_arch_impl()->get_noc_reg_base(CoreType::ARC, static_cast<uint32_t>(get_selected_noc_id())) +
                    get_arch_impl()->get_noc_node_id_offset();
    uint32_t value = 0;
    get_protocol()->read_from_device(&value, get_hang_check_core(noc), addr, sizeof(value));
    return value;
}

tt_xy_pair WormholeHangDetector::get_hang_check_core(NocId noc) const {
    return (noc == NocId::NOC1) ? tt_xy_pair(
                                      wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                                      wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y])
                                : wormhole::ARC_CORES_NOC0[0];
}

}  // namespace tt::umd
