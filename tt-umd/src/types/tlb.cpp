// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/types/tlb.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace tt::umd {

namespace {

// One field of a TLB configuration register: its value, first bit and width. A width of 0 means the
// architecture's register has no such field.
struct TlbField {
    uint64_t value;
    uint32_t start;
    uint32_t width;
};

constexpr uint32_t COORDINATE_WIDTH = 6;

// Field order follows the register layout, LSB first. Each field runs up to the start of the next one
// it is packed against, except the single-bit flags and the trailing virtual channel fields whose
// widths the layout states separately.
std::array<TlbField, 12> get_fields(const tlb_data& data, const tlb_offsets& offset) {
    const uint32_t static_vc_width =
        offset.static_vc_end != 0 ? offset.static_vc_end - offset.static_vc : (offset.static_vc != 0 ? 1 : 0);
    const uint32_t static_vc_buddy_width = offset.static_vc_buddy != 0 ? 1 : 0;
    const uint32_t static_vc_class_width =
        offset.static_vc_class != 0 ? offset.static_vc_class_end - offset.static_vc_class : 0;

    return {{
        {data.local_offset, offset.local_offset, offset.x_end - offset.local_offset},
        {data.x_end, offset.x_end, COORDINATE_WIDTH},
        {data.y_end, offset.y_end, COORDINATE_WIDTH},
        {data.x_start, offset.x_start, COORDINATE_WIDTH},
        {data.y_start, offset.y_start, COORDINATE_WIDTH},
        {data.noc_sel, offset.noc_sel, offset.mcast - offset.noc_sel},
        {data.mcast, offset.mcast, 1},
        {data.ordering, offset.ordering, offset.linked - offset.ordering},
        {data.linked, offset.linked, 1},
        {data.static_vc, offset.static_vc, static_vc_width},
        {data.static_vc_buddy, offset.static_vc_buddy, static_vc_buddy_width},
        {data.static_vc_class, offset.static_vc_class, static_vc_class_width},
    }};
}

bool fits(const TlbField& field) {
    if (field.width >= 64) {
        return true;
    }
    return (field.value >> field.width) == 0;
}

}  // namespace

void tlb_data::set_static_vc(const tlb_static_vc& vc) {
    static_vc = vc.static_vc;
    static_vc_buddy = vc.static_vc_buddy;
    static_vc_class = vc.static_vc_class;
}

bool tlb_data::check(const tlb_offsets& offset) const {
    for (const TlbField& field : get_fields(*this, offset)) {
        if (!fits(field)) {
            return false;
        }
    }
    return true;
}

std::pair<std::uint64_t, std::uint64_t> tlb_data::apply_offset(const tlb_offsets& offset) const {
    uint64_t low = 0;
    uint64_t high = 0;

    for (const TlbField& field : get_fields(*this, offset)) {
        if (field.width == 0 || field.value == 0) {
            continue;
        }
        if (field.start < 64) {
            low |= field.value << field.start;
            // The part of the value that spills over bit 63 lands at the bottom of the high word.
            if (field.start + field.width > 64 && field.start != 0) {
                high |= field.value >> (64 - field.start);
            }
        } else {
            high |= field.value << (field.start - 64);
        }
    }

    return {low, high};
}

}  // namespace tt::umd
