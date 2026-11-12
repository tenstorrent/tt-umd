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
// SoCs whose interconnect does not, where the device tree declares no `dma-coherent` property.
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
// Availability and the cache block size are resolved once at run time. Zicbom is optional in
// rv64gc and its block size is implementation-defined, so neither can be assumed at compile time.
//
// Gating is on the platform's actual DMA coherency, not on the ISA and not on Zicbom merely being
// available: coherency is a property of the SoC interconnect, so a coherent RISC-V host must pay
// nothing. A device tree that declares `dma-coherent` means the platform snoops; anything unknown is
// treated as non-coherent, so being wrong costs redundant maintenance rather than stale data.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#if defined(__riscv) && (__riscv_xlen == 64)
#include <unistd.h>

#include <sys/syscall.h>
#if __has_include(<asm/hwprobe.h>)
#include <asm/hwprobe.h>
#define TT_UMD_HAVE_RISCV_HWPROBE 1
#endif
#endif

namespace tt::umd {

namespace host_cache {

#if defined(__riscv) && (__riscv_xlen == 64)

struct CboInfo {
    bool usable = false;
    uint32_t block_bytes = 0;
};

inline CboInfo probe_cbo() {
    CboInfo info;
#if defined(TT_UMD_HAVE_RISCV_HWPROBE) && defined(__NR_riscv_hwprobe) && \
    defined(RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE) && defined(RISCV_HWPROBE_EXT_ZICBOM)
    riscv_hwprobe pairs[2] = {
        {RISCV_HWPROBE_KEY_IMA_EXT_0, 0},
        {RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE, 0},
    };
    // The kernel only reports an extension here once it is usable from user mode, which is what
    // these call sites need -- Zicbom can be present in hardware but trapped at U-mode.
    if (::syscall(__NR_riscv_hwprobe, pairs, 2UL, 0UL, nullptr, 0U) != 0) {
        return info;
    }
    const auto block_bytes = static_cast<uint32_t>(pairs[1].value);
    if ((static_cast<uint64_t>(pairs[0].value) & RISCV_HWPROBE_EXT_ZICBOM) != 0 && block_bytes > 0 &&
        (block_bytes & (block_bytes - 1)) == 0) {
        info.usable = true;
        info.block_bytes = block_bytes;
    }
#endif
    return info;
}

inline const CboInfo& cbo_info() {
    static const CboInfo info = probe_cbo();
    return info;
}

// Walk up from a PCI device's sysfs node to the first ancestor that has an OF node, and report
// whether that node declares `dma-coherent`. A PCI endpoint has no OF node of its own unless the
// device tree describes it, so the walk is what lands on the PCIe controller -- the node whose
// coherency actually governs the card's inbound DMA.
inline bool of_ancestor_declares_coherent(const std::filesystem::path& start) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur = fs::canonical(start, ec);
    if (ec) {
        return false;
    }
    for (int depth = 0; depth < 8 && !cur.empty() && cur != cur.root_path(); ++depth) {
        const fs::path of_node = cur / "of_node";
        if (fs::exists(of_node, ec)) {
            return fs::exists(of_node / "dma-coherent", ec);
        }
        cur = cur.parent_path();
    }
    return false;
}

// True only when the platform is *known* to snoop this device's inbound DMA. Anything unknown is
// treated as non-coherent, so correctness does not depend on recognising every platform; the cost
// of being wrong that way is some redundant cache maintenance, not stale data.
//
// Checking for `dma-coherent` anywhere in the tree would be wrong: the SpacemiT K3 declares it on
// all eight of its internal hdma engines while leaving every pcie@* node without it, so a global
// search concludes "coherent" on exactly the board that is not.
inline bool platform_is_dma_coherent(const char* pci_bdf = nullptr) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path pci_devices{"/sys/bus/pci/devices"};
    if (pci_bdf != nullptr) {
        return of_ancestor_declares_coherent(pci_devices / pci_bdf);
    }
    // No BDF to hand: fall back to the first Tenstorrent device present.
    static constexpr std::string_view kTenstorrentVendorId = "0x1e52";
    for (const auto& dev : fs::directory_iterator(pci_devices, ec)) {
        if (ec) {
            break;
        }
        std::ifstream vendor(dev.path() / "vendor");
        std::string id;
        if (vendor && std::getline(vendor, id) && id == kTenstorrentVendorId) {
            return of_ancestor_declares_coherent(dev.path());
        }
    }
    return false;
}

// Everything the call sites need, resolved from one evaluation.
//
// `enabled == false` means two very different things, and conflating them is how a non-coherent host
// ends up silently corrupting sysmem: either the platform snoops and no maintenance is needed, or it
// does not snoop and we cannot do anything about it. `required_but_unavailable` reports the second
// case so callers can say so loudly instead of proceeding as though all were well.
struct Decision {
    bool enabled = false;
    bool required_but_unavailable = false;
    uint32_t block_bytes = 0;
};

// Resolved on first use rather than at load time: the probe costs a syscall and a sysfs walk, and a
// process that links UMD without ever touching sysmem should not pay for them. One function-local
// static for all three answers, so the walk happens once and the hot path reads plain fields instead
// of re-entering a guarded static per query.
inline const Decision& decision() {
    static const Decision resolved = [] {
        const CboInfo& cbo = cbo_info();
        const bool coherent = platform_is_dma_coherent();
        return Decision{
            .enabled = cbo.usable && !coherent,
            .required_but_unavailable = !cbo.usable && !coherent,
            .block_bytes = cbo.block_bytes,
        };
    }();
    return resolved;
}

inline bool available() { return decision().enabled; }

inline bool maintenance_required_but_unavailable() { return decision().required_but_unavailable; }

inline uint32_t block_bytes() { return decision().block_bytes; }

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
    // cbo.flush (write back + invalidate) rather than cbo.inval. The write side below cleans, so
    // sysmem holds no dirty host lines and the two are equivalent here -- but the ISA permits
    // cbo.inval to *discard* a dirty line, so flush is the option that cannot lose data if a future
    // writer reaches sysmem without cleaning. Note the host does write into the completion region:
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

#else

// Same shape as the riscv64 decision so the range functions below need no #if of their own, but
// constexpr, so every caller folds to nothing. Off riscv64 we cannot tell whether the platform
// snoops, and every host tt-umd currently runs on other than riscv64 does, so report no fault
// rather than a false alarm.
struct Decision {
    bool enabled = false;
    bool required_but_unavailable = false;
    uint32_t block_bytes = 0;
};

constexpr Decision decision() { return {}; }

constexpr bool available() { return false; }

constexpr bool maintenance_required_but_unavailable() { return false; }

constexpr uint32_t block_bytes() { return 0; }

inline void clean_block(const void*) {}

inline void invalidate_block(const void*) {}

inline void barrier() {}

inline void barrier_to_device() {}

#endif

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

// Call after the host writes [start, start + bytes) that the device will DMA-read.
inline void clean_range(const void* start, size_t bytes) {
    const Decision d = decision();
    if (!d.enabled) {
        return;
    }
    barrier();  // the stores being written back must be complete first
    for_each_block(start, bytes, d.block_bytes, [](const void* block) { clean_block(block); });
    barrier_to_device();  // ordered against the device-visible store that follows, not just memory
}

// Call before the host reads [start, start + bytes) that the device has DMA-written.
inline void invalidate_range(const void* start, size_t bytes) {
    const Decision d = decision();
    if (!d.enabled) {
        return;
    }
    barrier();
    for_each_block(start, bytes, d.block_bytes, [](const void* block) { invalidate_block(block); });
    barrier();  // order the invalidates against the loads that follow
}

}  // namespace host_cache

}  // namespace tt::umd
