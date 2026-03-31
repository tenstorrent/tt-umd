/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/blackhole_hang_detector.hpp"

#include "noc_access.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"

namespace tt::umd {

BlackholeHangDetector::BlackholeHangDetector(
    DeviceProtocol* protocol, architecture_implementation* arch_impl, tt_xy_pair pcie_core) :
    HangDetector(protocol, arch_impl, pcie_core) {}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_bar() {
    auto* pcie = dynamic_cast<PcieInterface*>(get_protocol());
    return pcie->bar_read32(get_arch_impl()->get_read_checking_offset());
}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_noc() {
    uint64_t addr = get_arch_impl()->get_noc_reg_base(CoreType::PCIE, static_cast<uint32_t>(get_selected_noc_id())) +
                    get_arch_impl()->get_noc_node_id_offset();
    uint32_t value = 0;
    get_protocol()->read_from_device(&value, get_hang_check_core(), addr, sizeof(value));
    return value;
}

}  // namespace tt::umd
