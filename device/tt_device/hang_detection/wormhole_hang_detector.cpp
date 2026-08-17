/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/wormhole_hang_detector.hpp"

#include <vector>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

WormholeHangDetector::WormholeHangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl) :
    HangDetectorImplementation(protocol, arch_impl) {}

uint32_t WormholeHangDetector::read_hang_check_reg_via_bar() {
    return get_pcie_interface()->bar_read32(get_arch_impl()->get_read_checking_offset());
}

uint32_t WormholeHangDetector::read_hang_check_reg_via_noc(NocId noc) {
    tt_xy_pair core = get_hang_check_core(noc);
    uint64_t addr = get_arch_impl()->get_noc_reg_base(CoreType::ARC, static_cast<uint32_t>(noc)) +
                    get_arch_impl()->get_noc_node_id_offset();
    return read_noc_reg(core, addr, noc);
}

tt_xy_pair WormholeHangDetector::get_hang_check_core(NocId noc) {
    return (noc == NocId::NOC1) ? tt_xy_pair(
                                      wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                                      wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y])
                                : wormhole::ARC_CORES_NOC0[0];
}

}  // namespace tt::umd
