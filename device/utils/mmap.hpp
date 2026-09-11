// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/mman.h>

namespace tt::umd {

// Prefaulting is an optimization; platforms without MAP_POPULATE fault pages in
// on demand. Keep Linux's existing allocation behavior.
#ifdef MAP_POPULATE
inline constexpr int mmap_populate = MAP_POPULATE;
#else
inline constexpr int mmap_populate = 0;
#endif

}  // namespace tt::umd
