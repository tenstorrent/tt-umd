// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/types/arch.hpp"

namespace tt::umd {

// Smallest window size the architecture provides that reaches size bytes from addr, so callers need not
// know the architecture's window sizes. A size of 0 is returned as is, leaving the choice to the allocator.
size_t fit_tlb_window_size(const tt::ARCH arch, uint64_t addr, size_t size);

}  // namespace tt::umd
