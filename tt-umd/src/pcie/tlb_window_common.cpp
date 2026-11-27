// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/tlb_window_common.hpp"

#include <fmt/format.h>

#include <stdexcept>
#include <string_view>

#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

// IoOrdering carries the TLB hardware encoding directly, so translating between the two is a cast.
// If these ever diverge, the casts in make_tlb_config()/get_tlb_ordering() below and in
// PcieProtocol::export_window() become silently wrong.
// TEMP: these live exactly as long as those casts do, i.e. until tlb_data is gone from the
// window API and IoOrdering is what callers pass all the way down. tlb_data itself stays either way --
// it is the hardware register descriptor (see tlb_data::apply_offset) and remains internal to the
// handle/PCI layer.
static_assert(static_cast<uint64_t>(IoOrdering::RELAXED) == tlb_data::Relaxed);
static_assert(static_cast<uint64_t>(IoOrdering::STRICT) == tlb_data::Strict);
static_assert(static_cast<uint64_t>(IoOrdering::POSTED) == tlb_data::Posted);

uint64_t configure_tlb_aligned(TlbHandle& handle, const tlb_data& config) {
    const uint64_t aligned_addr = config.local_offset & ~(handle.get_size() - 1);
    tlb_data aligned_config = config;
    aligned_config.local_offset = aligned_addr;
    handle.configure(aligned_config);
    return config.local_offset - aligned_addr;
}

void validate_tlb_window_range(uint64_t offset, size_t size, size_t window_size) {
    if ((offset + size) > window_size) {
        throw std::out_of_range("Out of bounds access");
    }
}

void validate_tlb_window_aligned(uint64_t offset, size_t size, std::string_view operation) {
    UMD_ASSERT(
        offset % sizeof(uint32_t) == 0 && size % sizeof(uint32_t) == 0,
        error::RuntimeError,
        fmt::format("{} offset and size must be 4-byte aligned.", operation));
}

IoOrdering default_tlb_ordering(bool carries_ordering) {
    return carries_ordering ? IoOrdering::STRICT : IoOrdering::RELAXED;
}

tlb_data make_tlb_config(
    tt::ARCH arch,
    const TargetIoWindowConfig& config,
    IoOrdering ordering,
    WindowFlags supported_flags,
    bool carries_ordering) {
    // The direction field is encoded into the TLB mapping; anything else must be honored by the
    // window. Flags it cannot honor are rejected; failing loudly beats silently dropping them.
    UMD_ASSERT(
        (config.flags & ~(WindowFlags::DirectionMask | supported_flags)) == WindowFlags::None,
        error::RuntimeError,
        "Requested WindowFlags are not supported by this TLB-backed IoWindow.");

    UMD_ASSERT(config.noc.has_value(), error::RuntimeError, "TLB-backed IoWindows must specify a NOC.");

    // An ordering this window has no field for is refused rather than recorded and silently not applied.
    UMD_ASSERT(
        ordering == IoOrdering::RELAXED || carries_ordering,
        error::RuntimeError,
        "This IoWindow carries no transaction ordering, so only Relaxed can be honored.");

    const bool mcast = config.core_end.has_value();
    const tt_xy_pair core_end = mcast ? config.core_end.value() : config.core_start;

    tlb_data tlb_config{};
    tlb_config.local_offset = config.addr;
    tlb_config.x_end = core_end.x;
    tlb_config.y_end = core_end.y;
    tlb_config.noc_sel = static_cast<uint64_t>(config.noc.value());
    tlb_config.ordering = static_cast<uint64_t>(ordering);
    tlb_config.set_static_vc(get_architecture_tlbs(arch).get_static_vc(config.flags));
    // Carried in the window's own configuration rather than on the transaction, so an
    // architecture that has nowhere to put it rejects the flag before reaching here.
    tlb_config.noncacheable = (config.flags & WindowFlags::NonCacheable) != WindowFlags::None ? 1 : 0;
    if (mcast) {
        tlb_config.mcast = true;
        tlb_config.x_start = config.core_start.x;
        tlb_config.y_start = config.core_start.y;
    }
    return tlb_config;
}

TargetIoWindowConfig make_target_config(const tlb_data& config, uint64_t base_address, WindowFlags window_flags) {
    const tt_xy_pair end_core(config.x_end, config.y_end);

    TargetIoWindowConfig target;
    if (config.mcast) {
        target.core_start = tt_xy_pair(config.x_start, config.y_start);
        target.core_end = end_core;
    } else {
        target.core_start = end_core;
    }
    target.addr = base_address;
    target.noc = static_cast<NocId>(config.noc_sel);
    target.flags = window_flags;
    return target;
}

IoOrdering get_tlb_ordering(const tlb_data& config) { return static_cast<IoOrdering>(config.ordering); }

HostMemoryCaching get_tlb_memory_caching(TlbMapping mapping) {
    return mapping == TlbMapping::WC ? HostMemoryCaching::WC : HostMemoryCaching::UC;
}

}  // namespace tt::umd
