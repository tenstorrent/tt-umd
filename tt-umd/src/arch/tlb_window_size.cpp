// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "arch/tlb_window_size.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <vector>

#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

size_t fit_tlb_window_size(const tt::ARCH arch, uint64_t addr, size_t size) {
    if (size == 0) {
        return 0;
    }
    // A window is anchored at the start of an aligned block of its own size, so the span reachable from addr
    // is what is left of that block -- the class has to cover the address's offset into it as well. Size
    // classes are ordered smallest first, so the first fit is the smallest.
    const std::vector<TlbSizeClass>& size_classes = get_architecture_tlbs(arch).size_classes;
    auto size_class =
        std::find_if(size_classes.begin(), size_classes.end(), [addr, size](const TlbSizeClass& candidate) {
            // Written as a subtraction so a huge requested size cannot overflow the sum.
            return size <= candidate.size - (addr % candidate.size);
        });
    UMD_ASSERT(
        size_class != size_classes.end(),
        error::RuntimeError,
        fmt::format(
            "Requested I/O window of {} bytes at address {:#x} does not fit in the largest window {} provides "
            "({} bytes).",
            size,
            addr,
            arch_to_str(arch),
            size_classes.back().size));
    return size_class->size;
}

}  // namespace tt::umd
