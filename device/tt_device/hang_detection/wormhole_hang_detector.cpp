/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/wormhole_hang_detector.hpp"

#include "noc_access.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"

namespace tt::umd {

WormholeHangDetector::WormholeHangDetector(
    DeviceProtocol* protocol, architecture_implementation* arch_impl, tt_xy_pair arc_core) :
    HangDetector(protocol, arch_impl, arc_core) {}

uint32_t WormholeHangDetector::read_hang_check_reg_via_bar() {
    auto* pcie = dynamic_cast<PcieInterface*>(protocol_);
    return pcie->bar_read32(arch_impl_->get_read_checking_offset());
}

uint32_t WormholeHangDetector::read_hang_check_reg_via_noc() {
    uint64_t addr = arch_impl_->get_noc_reg_base(CoreType::ARC, static_cast<uint32_t>(get_selected_noc_id())) +
                    arch_impl_->get_noc_node_id_offset();
    uint32_t value = 0;
    protocol_->read_from_device(&value, hang_check_core_, addr, sizeof(value));
    return value;
}

}  // namespace tt::umd
