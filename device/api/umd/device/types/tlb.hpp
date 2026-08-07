// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

#include "umd/device/types/arch.hpp"

namespace tt::umd {

struct tlb_offsets {
    uint32_t local_offset;
    uint32_t x_end;
    uint32_t y_end;
    uint32_t x_start;
    uint32_t y_start;
    uint32_t noc_sel;
    uint32_t mcast;
    uint32_t ordering;
    uint32_t linked;
    uint32_t static_vc;
    uint32_t static_vc_end;
    // Per-TLB pin of the NoC virtual channel buddy bit and class bits, used when static_vc is set.
    // Only defined on architectures that expose them as configuration bits (Blackhole/Grendel).
    // Wormhole's PCIe tile hardwires the equivalent in hardware regardless of config, so these
    // offsets are left unset (0) there and no buddy/class bits are packed.
    uint32_t static_vc_buddy;
    uint32_t static_vc_class;
    uint32_t static_vc_class_end;
};

// Direction of a TLB transaction, used to pin the static VC so reads and writes never share a
// virtual channel (a single static VC carrying both crashes the host on Blackhole).
enum class TlbVcDirection {
    UnicastWrite,
    UnicastRead,
    MulticastWrite,
};

struct tlb_data {
    uint64_t local_offset = 0;
    uint64_t x_end = 0;
    uint64_t y_end = 0;
    uint64_t x_start = 0;
    uint64_t y_start = 0;
    uint64_t noc_sel = 0;
    uint64_t mcast = 0;
    uint64_t ordering = 0;
    uint64_t linked = 0;
    uint64_t static_vc = 0;
    uint64_t static_vc_buddy = 0;
    uint64_t static_vc_class = 0;

    // Orderings.
    static constexpr uint64_t Relaxed = 0;
    static constexpr uint64_t Strict = 1;
    static constexpr uint64_t Posted = 2;

    bool check(const tlb_offsets &offset) const;
    std::pair<std::uint64_t, std::uint64_t> apply_offset(const tlb_offsets &offset) const;
};

// Populates the static VC fields of `config` for the given architecture and transaction direction.
// Blackhole/Grendel expose buddy/class as config bits, so they are pinned by direction (write=buddy0,
// read=buddy1, multicast=class 0b10, unicast=class 0b00) to keep reads and writes on separate static
// VCs. Wormhole hardwires the equivalent in hardware, so only static_vc is set there. Architectures
// that do not use static VC are left on dynamic VC (static_vc=0).
void set_static_vc(tlb_data &config, tt::ARCH arch, TlbVcDirection direction);

struct tlb_configuration {
    uint64_t size;
    uint64_t base;
    uint64_t cfg_addr;
    uint64_t index_offset;
    uint64_t tlb_offset;
    tlb_offsets offset;
};

enum TlbMapping : uint8_t {
    UC = 0,  // Uncached
    WC = 1,  // Write-combined
};

}  // namespace tt::umd
