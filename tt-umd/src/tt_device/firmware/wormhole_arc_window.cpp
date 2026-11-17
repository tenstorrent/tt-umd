// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Wormhole is not supported by this build, so neither ARC window can be opened.

#include "tt-umd/tt_device/firmware/wormhole_arc_window.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_wormhole_arc_window_not_supported() {
    UMD_THROW(error::RuntimeError, "Wormhole ARC windows are not supported.");
}

}  // namespace

WormholeArcWindow WormholeArcWindow::arc_apb(DeviceProtocol*, PcieInterface*, JtagInterface*, RemoteInterface*) {
    throw_wormhole_arc_window_not_supported();
}

WormholeArcWindow WormholeArcWindow::arc_csm(DeviceProtocol*, PcieInterface*, JtagInterface*, RemoteInterface*) {
    throw_wormhole_arc_window_not_supported();
}

void WormholeArcWindow::read(void*, uint64_t, size_t, tt_xy_pair, NocId) { throw_wormhole_arc_window_not_supported(); }

void WormholeArcWindow::write(const void*, uint64_t, size_t, tt_xy_pair, NocId) {
    throw_wormhole_arc_window_not_supported();
}

}  // namespace tt::umd
