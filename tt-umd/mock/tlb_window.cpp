// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/tlb_window.hpp"

namespace tt::umd {

void TlbWindow::configure(const tlb_data&) {}

uint64_t TlbWindow::get_base_address() const { return 0; }

}  // namespace tt::umd
