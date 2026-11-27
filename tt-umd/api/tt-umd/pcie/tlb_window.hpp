// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Deprecated header: include "tt-umd/pcie/silicon_tlb_window.hpp" instead.
#include "tt-umd/pcie/silicon_tlb_window.hpp"

namespace tt::umd {

using TlbWindow [[deprecated("Use SiliconTlbWindow instead.")]] = SiliconTlbWindow;

}  // namespace tt::umd
