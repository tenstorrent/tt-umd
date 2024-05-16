// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device/blackhole_implementation.h"

namespace tt::umd {

std::tuple<xy_pair, xy_pair> blackhole_implementation::multicast_workaround(xy_pair start, xy_pair end) const {
    // TODO: This is copied from wormhole_implementation. It should be implemented properly.

    // When multicasting there is a rare case where including the multicasting node in the box can result in a backup
    // and the multicasted data not reaching all endpoints specified. As a workaround we exclude the pci endpoint from
    // the multicast. This doesn't cause any problems with making some tensix cores inaccessible because column 0 (which
    // we are excluding) doesn't have tensix.
    start.x = start.x == 0 ? 1 : start.x;
    return std::make_tuple(start, end);
}

tlb_configuration blackhole_implementation::get_tlb_configuration(uint32_t tlb_index) const {
    return tlb_configuration{
        .size = blackhole::DYNAMIC_TLB_2M_SIZE,
        .base = blackhole::DYNAMIC_TLB_2M_BASE,
        .cfg_addr = blackhole::DYNAMIC_TLB_2M_CFG_ADDR,
        .index_offset = tlb_index - blackhole::TLB_BASE_INDEX_2M,
        .offset = blackhole::TLB_2M_OFFSET,
    };
}

std::optional<std::tuple<std::uint32_t, std::uint32_t>> blackhole_implementation::describe_tlb(
    std::int32_t tlb_index) const {
    std::uint32_t TLB_COUNT_2M = 202;

    std::uint32_t TLB_BASE_2M = 0;
    if (tlb_index < 0) {
        return std::nullopt;
    }

    // Only have 2MB TLBs for now
    if (tlb_index >= 0 && tlb_index < TLB_BASE_2M) {
        auto tlb_offset = tlb_index;
        auto size = 1 << 21;
        return std::tuple(TLB_BASE_2M + tlb_offset * size, size);
    }

    return std::nullopt;
}

std::pair<std::uint64_t, std::uint64_t> blackhole_implementation::get_tlb_data(
    std::uint32_t tlb_index, const tlb_data& data) const {

    if (tlb_index < blackhole::TLB_COUNT_2M) {
        return data.apply_offset(blackhole::TLB_2M_OFFSET);
    } else {
        throw std::runtime_error("Invalid TLB index for Blackhole arch");
    }

}

}  // namespace tt::umd
