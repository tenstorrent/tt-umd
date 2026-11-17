// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Host cache maintenance for sysmem accessed by both the host and the device.
//
// The host maps sysmem (hugepages or pinned user pages) write-back cacheable. Where the platform
// does not snoop inbound PCIe DMA, the two sides disagree: a host read can keep returning a cached
// line the device has since overwritten, and a host write can sit dirty in cache where the device's
// DMA read of DRAM never sees it. x86 hosts snoop, so this is a no-op there; it is reachable on
// SoCs whose interconnect does not.
//
// The two directions are deliberately paired and asymmetric:
//
//   host write -> device read :  clean (write back), so DRAM becomes authoritative
//   device write -> host read :  invalidate, so the next load refetches from DRAM
//
// Pairing them is what makes the read side safe. If every host write to sysmem writes back, then
// sysmem never holds a dirty host line, and the read-side maintenance can neither discard host data
// nor write stale host bytes back over an entry the device has DMA'd in. Callers that write sysmem
// through their own mapping rather than through SysmemManager/SysmemBuffer must clean it themselves.
//
// This header is the mechanism only. Whether maintenance is needed is decided once in
// host_cache.cpp, which probes the ISA for usable cache-block operations and asks the platform
// whether it snoops. Keeping that out of the header is deliberate: it is detection, it runs once,
// and it would otherwise drag <filesystem> and <fstream> into every consumer of a public header.

#include <cstddef>
#include <cstdint>

namespace tt::umd {

namespace host_cache {

#if defined(__riscv) && (__riscv_xlen == 64)

// Resolved once on first use; see host_cache.cpp. Three states, not two: `available()` false means
// either the platform snoops and nothing is needed, or it does not snoop and we cannot help, which
// `maintenance_required_but_unavailable()` reports so callers can say so rather than proceed
// silently.
bool available();
uint32_t block_bytes();
bool maintenance_required_but_unavailable();

namespace detail {

// Emitted under a local `.option arch` so the rest of the build keeps its base -march; Zicbom need
// not be in it.
inline void clean_block(const void* addr) {
    __asm__ volatile(
        ".option push\n\t"
        ".option arch, +zicbom\n\t"
        "cbo.clean 0(%0)\n\t"
        ".option pop"
        :
        : "r"(addr)
        : "memory");
}

inline void invalidate_block(const void* addr) {
    // cbo.flush (write back + invalidate) rather than cbo.inval. The write side cleans, so sysmem
    // holds no dirty host lines and the two are equivalent here -- but the ISA permits cbo.inval to
    // *discard* a dirty line, so flush is the option that cannot lose data if a future writer
    // reaches sysmem without cleaning. Note the host does write into the completion region:
    // SystemMemoryManager mirrors the completion-queue read pointer there for post-hang forensics.
    __asm__ volatile(
        ".option push\n\t"
        ".option arch, +zicbom\n\t"
        "cbo.flush 0(%0)\n\t"
        ".option pop"
        :
        : "r"(addr)
        : "memory");
}

inline void barrier() { __asm__ volatile("fence rw,rw" ::: "memory"); }

// Ordering for the write side specifically. `fence rw,rw` orders memory against memory and says
// nothing about device I/O, but the store that tells the device to fetch cleaned bytes is an MMIO
// write. `cbo.clean` is ordered as a store to its block, so predecessor `w` covers it and successor
// `o` covers the doorbell. Callers already issue their own barrier before ringing a doorbell; this
// makes the primitive correct on its own rather than dependent on one.
inline void barrier_to_device() { __asm__ volatile("fence ow,ow" ::: "memory"); }

}  // namespace detail

#else

// On architectures other than riscv64 we have no way to tell whether the platform snoops, and every
// such host tt-umd currently runs on does, so report no fault rather than a false alarm. constexpr,
// so the range functions below fold to nothing and no call survives.
constexpr bool available() { return false; }

constexpr uint32_t block_bytes() { return 0; }

constexpr bool maintenance_required_but_unavailable() { return false; }

namespace detail {

inline void clean_block(const void*) {}

inline void invalidate_block(const void*) {}

inline void barrier() {}

inline void barrier_to_device() {}

}  // namespace detail

#endif

namespace detail {

// Apply `op` to every cache block overlapping [start, start + bytes). Striding byte offsets from
// start instead would leave the final block untouched whenever start is not block-aligned.
template <typename Op>
inline void for_each_block(const void* start, size_t bytes, uint32_t stride, Op op) {
    if (bytes == 0) {
        return;
    }
    const auto addr = reinterpret_cast<uintptr_t>(start);
    const auto mask = ~static_cast<uintptr_t>(stride - 1);
    for (uintptr_t block = addr & mask; block <= ((addr + bytes - 1) & mask); block += stride) {
        op(reinterpret_cast<const void*>(block));
    }
}

}  // namespace detail

// Call after the host writes [start, start + bytes) that the device will DMA-read.
inline void clean_range(const void* start, size_t bytes) {
    if (!available()) {
        return;
    }
    detail::barrier();  // the stores being written back must be complete first
    detail::for_each_block(start, bytes, block_bytes(), [](const void* block) {
        detail::clean_block(block);
    });
    detail::barrier_to_device();  // ordered against the device-visible store that follows
}

// Call before the host reads [start, start + bytes) that the device has DMA-written.
inline void invalidate_range(const void* start, size_t bytes) {
    if (!available()) {
        return;
    }
    detail::barrier();
    detail::for_each_block(start, bytes, block_bytes(), [](const void* block) {
        detail::invalidate_block(block);
    });
    detail::barrier();  // order the invalidates against the loads that follow
}

}  // namespace host_cache

}  // namespace tt::umd
