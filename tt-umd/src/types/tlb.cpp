// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/types/tlb.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace tt::umd {

namespace {

// A field of the TLB configuration register: its value and the bit range [start, end) it occupies.
// A field the architecture's register lacks has an empty range and must stay zero.
struct RegisterField {
    uint64_t value;
    uint32_t start;
    uint32_t end;

    uint32_t width() const { return end > start ? end - start : 0; }
};

// Each field ends where the next one in the register begins. The static VC fields at the top are the
// exception: their ends are given explicitly, because the architectures differ in which of them exist.
std::array<RegisterField, 12> get_register_fields(const tlb_data &data, const tlb_offsets &offset) {
    const uint32_t static_vc_end = offset.static_vc_end != 0 ? offset.static_vc_end : offset.static_vc_buddy;
    return {{
        {data.local_offset, offset.local_offset, offset.x_end},
        {data.x_end, offset.x_end, offset.y_end},
        {data.y_end, offset.y_end, offset.x_start},
        {data.x_start, offset.x_start, offset.y_start},
        {data.y_start, offset.y_start, offset.noc_sel},
        {data.noc_sel, offset.noc_sel, offset.mcast},
        {data.mcast, offset.mcast, offset.ordering},
        {data.ordering, offset.ordering, offset.linked},
        {data.linked, offset.linked, offset.static_vc},
        {data.static_vc, offset.static_vc, static_vc_end},
        {data.static_vc_buddy, offset.static_vc_buddy, offset.static_vc_class},
        {data.static_vc_class, offset.static_vc_class, offset.static_vc_class_end},
    }};
}

}  // namespace

void tlb_data::set_static_vc(const tlb_static_vc &vc) {
    static_vc = vc.static_vc;
    static_vc_buddy = vc.static_vc_buddy;
    static_vc_class = vc.static_vc_class;
}

bool tlb_data::check(const tlb_offsets &offset) const {
    for (const RegisterField &field : get_register_fields(*this, offset)) {
        const uint32_t width = field.width();
        if (width < 64 && (field.value >> width) != 0) {
            return false;
        }
    }
    return true;
}

std::pair<std::uint64_t, std::uint64_t> tlb_data::apply_offset(const tlb_offsets &offset) const {
    // Blackhole's register is wider than 64 bits, so it is assembled as one 128-bit value and returned as
    // its low and high halves.
    unsigned __int128 reg = 0;
    for (const RegisterField &field : get_register_fields(*this, offset)) {
        if (field.width() != 0) {
            reg |= static_cast<unsigned __int128>(field.value) << field.start;
        }
    }
    return {static_cast<uint64_t>(reg), static_cast<uint64_t>(reg >> 64)};
}

}  // namespace tt::umd
