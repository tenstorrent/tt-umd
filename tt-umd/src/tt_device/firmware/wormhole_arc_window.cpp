// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/firmware/wormhole_arc_window.hpp"

#include <cstddef>
#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

namespace {

constexpr const char* WORMHOLE_ARC = "Wormhole ARC access";

}  // namespace

WormholeArcWindow WormholeArcWindow::arc_apb(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) {
    UMD_NOT_SUPPORTED(WORMHOLE_ARC);
}

WormholeArcWindow WormholeArcWindow::arc_csm(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) {
    UMD_NOT_SUPPORTED(WORMHOLE_ARC);
}

void WormholeArcWindow::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    UMD_NOT_SUPPORTED(WORMHOLE_ARC);
}

void WormholeArcWindow::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    UMD_NOT_SUPPORTED(WORMHOLE_ARC);
}

}  // namespace tt::umd
