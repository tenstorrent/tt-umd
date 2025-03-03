// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/tlb_window.h"

namespace tt::umd {

TlbWindow::TlbWindow(TlbNocConfig tlb_noc_config, uint32_t uid, void* ptr) : tlb_noc_config(tlb_noc_config), ptr(ptr) {}

}  // namespace tt::umd
