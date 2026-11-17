// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/hang_detection/blackhole_hang_detector.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include "tt-umd/arch/architecture_registers.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/io_window_config.hpp"

namespace tt::umd {

BlackholeHangDetector::BlackholeHangDetector(
    PcieInterface* pcie_interface,
    std::unique_ptr<TlbWindow> probe_window,
    tt_xy_pair arc_core_noc0,
    tt_xy_pair arc_core_noc1) :
    pcie_interface_(pcie_interface),
    probe_window_(std::move(probe_window)),
    arc_core_noc0_(arc_core_noc0),
    arc_core_noc1_(arc_core_noc1) {}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_bar() {
    return pcie_interface_->bar_read32(get_architecture_registers(tt::ARCH::BLACKHOLE).noc_node_id_bar_offset);
}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_noc(NocId noc) {
    TargetIoWindowConfig target{};
    target.core_start = noc == NocId::NOC1 ? arc_core_noc1_ : arc_core_noc0_;
    target.addr = get_architecture_registers(tt::ARCH::BLACKHOLE)
                      .get_noc_node_id_reg_addr(CoreType::ARC, static_cast<uint32_t>(noc), 0);
    target.noc = noc;
    std::lock_guard<std::mutex> lock(probe_window_mutex_);
    probe_window_->configure(target);
    return probe_window_->read32(0);
}

bool BlackholeHangDetector::is_bus_available() { return pcie_interface_ != nullptr; }

bool BlackholeHangDetector::is_noc_available() { return probe_window_ != nullptr; }

}  // namespace tt::umd
