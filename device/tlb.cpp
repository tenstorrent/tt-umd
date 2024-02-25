// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device/tlb.h"

namespace tt::umd {

bool tlb_data::check(const tlb_offsets &offset) const {
    return local_offset > ((1 << (offset.x_end - offset.local_offset)) - 1) |
           x_end > ((1 << (offset.y_end - offset.x_end)) - 1) | y_end > ((1 << (offset.x_start - offset.y_end)) - 1) |
           x_start > ((1 << (offset.y_start - offset.x_start)) - 1) |
           y_start > ((1 << (offset.noc_sel - offset.y_start)) - 1) |
           noc_sel > ((1 << (offset.mcast - offset.noc_sel)) - 1) |
           mcast > ((1 << (offset.ordering - offset.mcast)) - 1) |
           ordering > ((1 << (offset.linked - offset.ordering)) - 1) |
           linked > ((1 << (offset.static_vc - offset.linked)) - 1) |
           static_vc > ((1 << (offset.static_vc_end - offset.static_vc)) - 1);
}

std::optional<std::uint64_t> tlb_data::apply_offset(const tlb_offsets &offset) const {
    if (this->check(offset)) {
        return std::nullopt;
    }

    return local_offset << offset.local_offset | x_end << offset.x_end | y_end << offset.y_end |
           x_start << offset.x_start | y_start << offset.y_start | noc_sel << offset.noc_sel | mcast << offset.mcast |
           ordering << offset.ordering | linked << offset.linked | static_vc << offset.static_vc;
}

}  // namespace tt::umd
