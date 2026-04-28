// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/sw_emule_chip.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "tt_emule/asan_bridge.h"
#include "tt_emule/device.hpp"
#include "tt_emule/l1_pool.hpp"

// Weak no-op fallbacks for the ASan poisoning bridge. tt-emule provides
// strong definitions of these in src/kernel_runner.cpp; when an executable
// links tt-emule (directly or via libtt_metal.so), those override these. UMD
// libraries and standalone tools that don't pull in tt-emule fall back to
// these no-ops — correct behavior, because there's no ASan poisoning context
// to act on without tt-emule's L1Pool / Core memory management.
//
// Without these, libtt-umd.so has unresolved references to
// __emule_buffer_alloc/free (introduced by SWEmuleChip::initialize_asan_poison
// and get_core's poison hook). Tools like tt-metal's tools/scaleout/* and
// UMD's own diagnostic tools that link libtt-umd.so without tt-emule fail
// the link.
extern "C" {
__attribute__((weak)) void __emule_buffer_alloc(uint8_t*, std::size_t) {}
__attribute__((weak)) void __emule_buffer_free(uint8_t*, std::size_t) {}
}

namespace tt::umd {

// SOC-derived DRAM bank size, captured at the most recent SWEmuleChip
// construction. The JIT bridge function `__emule_dram_ptr` reads this for
// bounds checking. Atomic for thread safety; assumes a single architecture
// per process (multi-arch fan-out would need per-chip lookup instead).
namespace {
std::atomic<uint64_t> g_active_dram_bank_size{0};
}

uint64_t SWEmuleChip::active_dram_bank_size() {
    return g_active_dram_bank_size.load(std::memory_order_acquire);
}

// Out-of-line destructor — tt_emule::Core and L1Pool must be complete for unique_ptr destruction.
SWEmuleChip::~SWEmuleChip() = default;

SWEmuleChip::SWEmuleChip(SocDescriptor soc_descriptor) : Chip(std::move(soc_descriptor)) {
    auto& soc = get_soc_descriptor();

    l1_size_ = soc.worker_l1_size;
    // Use full DRAM bank size — DRAM cores use regular mmap (not MAP_32BIT),
    // so virtual address space is not constrained.  Wormhole views use address
    // offsets up to 1 GB within a 2 GB bank, so capping below bank size causes
    // writes to segfault.  Overcommit means only touched pages use physical RAM.
    dram_bank_size_ = soc.dram_bank_size;
    g_active_dram_bank_size.store(dram_bank_size_, std::memory_order_release);

    // Build DRAM core lookup table from SOC descriptor.
    auto dram_cores = soc.get_dram_cores();
    for (uint32_t channel = 0; channel < dram_cores.size(); ++channel) {
        for (const auto& core : dram_cores[channel]) {
            dram_core_to_channel_[tt_xy_pair(core.x, core.y)] = channel;
        }
    }

    // Allocate L1Pool for worker cores.
    // Use a generous count covering Tensix + Ethernet + Router + other non-DRAM cores,
    // since all non-DRAM cores go through the pool for consistent bitmask offset extraction.
    // Add extra headroom for cores created via translated coords that differ from physical coords.
    size_t num_tensix = soc.get_cores(tt::CoreType::TENSIX).size();
    // 128 is a safe upper bound on Tensix cores across known architectures (Wormhole=72,
    // Blackhole~120). Used as fallback if SOC descriptor reports zero.
    size_t pool_size = (num_tensix > 0 ? num_tensix : 128) * 2;  // 2× headroom
    // Pass l1_size_ as the live region per slot so the unused 1 MB tail of each
    // 2 MB slot is poisoned under ASan — kernels writing past L1_SIZE trip immediately.
    worker_pool_ = std::make_unique<tt_emule::L1Pool>(pool_size, static_cast<size_t>(l1_size_));
}

bool SWEmuleChip::is_dram_core(tt_xy_pair core_xy) const { return dram_core_to_channel_.count(core_xy) > 0; }

tt_emule::Core* SWEmuleChip::get_core(tt_xy_pair core_xy) {
    std::lock_guard<std::mutex> lock(core_mutex_);

    auto it = cores_.find(core_xy);
    if (it != cores_.end()) {
        return it->second.get();
    }

    // Lazy-create with appropriate role and size.
    tt_emule::CoreCoord coord{core_xy.x, core_xy.y};
    std::unique_ptr<tt_emule::Core> core;
    if (is_dram_core(core_xy)) {
        // DRAM cores: individual mmap (not from pool, not MAP_32BIT).
        core = std::make_unique<tt_emule::Core>(coord, tt_emule::CoreRole::DRAM, static_cast<size_t>(dram_bank_size_));
    } else if (next_slot_ < worker_pool_->num_slots()) {
        // Worker cores: allocate from L1Pool (aligned slots, bitmask offset extraction).
        size_t slot = next_slot_++;
        uint8_t* slot_mem = worker_pool_->slot_ptr(slot);
        core = std::make_unique<tt_emule::Core>(coord, slot_mem, static_cast<size_t>(l1_size_));
        core_to_slot_[core_xy] = slot;
    } else {
        // Pool exhausted — fall back to individual mmap (still MAP_32BIT via CoreRole::WORKER).
        log_warning(
            LogUMD,
            "L1Pool exhausted (all {} slots used); core ({},{}) falling back to individual mmap",
            worker_pool_->num_slots(),
            core_xy.x,
            core_xy.y);
        core = std::make_unique<tt_emule::Core>(coord, tt_emule::CoreRole::WORKER, static_cast<size_t>(l1_size_));
    }

    tt_emule::Core* raw_ptr = core.get();
    cores_[core_xy] = std::move(core);

    // ASan: if initialize_asan_poison() was already called, poison this newly-
    // created core's allocator-managed region too. Sentinel UINT32_MAX means
    // "ASan poisoning not active" — keep get_core() side-effect-free.
    if (raw_ptr->role() == tt_emule::CoreRole::WORKER) {
        if (l1_unreserved_base_ != UINT32_MAX && l1_unreserved_base_ < l1_size_) {
            __emule_buffer_free(raw_ptr->l1_data() + l1_unreserved_base_, l1_size_ - l1_unreserved_base_);
        }
    } else {
        if (dram_unreserved_base_ != UINT32_MAX && dram_unreserved_base_ < dram_bank_size_) {
            __emule_buffer_free(
                raw_ptr->l1_data() + dram_unreserved_base_,
                static_cast<std::size_t>(dram_bank_size_ - dram_unreserved_base_));
        }
    }

    return raw_ptr;
}

tt_emule::Core* SWEmuleChip::core_for_logical(CoreCoord coord, bool is_dram) {
    if (is_dram) {
        // coord.x is the DRAM channel id — look up the registered DRAM core.
        for (const auto& [xy, channel] : dram_core_to_channel_) {
            if (channel == coord.x) {
                std::lock_guard<std::mutex> lock(core_mutex_);
                auto it = cores_.find(xy);
                return it == cores_.end() ? nullptr : it->second.get();
            }
        }
        return nullptr;
    }
    // Worker: coord is already the virtual NOC coord (caller did the
    // logical->virtual translation via IDevice).
    std::lock_guard<std::mutex> lock(core_mutex_);
    auto it = cores_.find(tt_xy_pair(coord.x, coord.y));
    return it == cores_.end() ? nullptr : it->second.get();
}

void SWEmuleChip::initialize_asan_poison(uint32_t l1_unreserved, uint32_t dram_unreserved) {
    std::lock_guard<std::mutex> lock(core_mutex_);
    // Store the bases so cores lazy-created later via get_core() inherit
    // the same poisoning.
    l1_unreserved_base_ = l1_unreserved;
    dram_unreserved_base_ = dram_unreserved;

    for (auto& [xy, core_uptr] : cores_) {
        tt_emule::Core* core = core_uptr.get();
        uint8_t* base = core->l1_data();
        if (core->role() == tt_emule::CoreRole::WORKER) {
            if (l1_unreserved < l1_size_) {
                __emule_buffer_free(base + l1_unreserved, l1_size_ - l1_unreserved);
            }
        } else {
            if (dram_unreserved < dram_bank_size_) {
                __emule_buffer_free(
                    base + dram_unreserved, static_cast<std::size_t>(dram_bank_size_ - dram_unreserved));
            }
        }
    }
}

void SWEmuleChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) {
    tt_xy_pair key(core.x, core.y);
    tt_emule::Core* target_core = get_core(key);
    std::memcpy(target_core->l1_ptr(static_cast<uint32_t>(l1_dest)), src, size);
}

void SWEmuleChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) {
    tt_xy_pair key(core.x, core.y);
    tt_emule::Core* target_core = get_core(key);
    std::memcpy(dest, target_core->l1_ptr(static_cast<uint32_t>(l1_src)), size);
}

// Register I/O forwards to the same memory path — emulated cores have no distinct
// register address space, so all offsets map into the same L1-backed storage.
void SWEmuleChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void SWEmuleChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

void SWEmuleChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    write_to_device(core, src, addr, size);
}

void SWEmuleChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    read_from_device(core, dst, addr, size);
}

// --- Chip lifecycle / hardware accessors (no-ops) ---

bool SWEmuleChip::is_mmio_capable() const { return false; }

void SWEmuleChip::start_device() {}

void SWEmuleChip::close_device() {}

TTDevice* SWEmuleChip::get_tt_device() { return nullptr; }

SysmemManager* SWEmuleChip::get_sysmem_manager() { return nullptr; }

TLBManager* SWEmuleChip::get_tlb_manager() { return nullptr; }

// --- Host memory (no-ops) ---

int SWEmuleChip::get_num_host_channels() { return 0; }

int SWEmuleChip::get_host_channel_size(std::uint32_t) { return 0; }

void SWEmuleChip::write_to_sysmem(uint16_t, const void*, uint64_t, uint32_t) {}

void SWEmuleChip::read_from_sysmem(uint16_t, void*, uint64_t, uint32_t) {}

// --- Multicast (not implemented) ---

void SWEmuleChip::dma_multicast_write(void*, size_t, CoreCoord, CoreCoord, uint64_t) {
    throw std::runtime_error("SWEmuleChip::dma_multicast_write is not implemented");
}

void SWEmuleChip::noc_multicast_write(void*, size_t, CoreCoord, CoreCoord, uint64_t) {
    throw std::runtime_error("SWEmuleChip::noc_multicast_write is not implemented");
}

// --- Barriers, resets, power (no-ops) ---

void SWEmuleChip::wait_for_non_mmio_flush() {}

void SWEmuleChip::l1_membar(const std::unordered_set<CoreCoord>&) {}

void SWEmuleChip::dram_membar(const std::unordered_set<CoreCoord>&) {}

void SWEmuleChip::dram_membar(const std::unordered_set<uint32_t>&) {}

void SWEmuleChip::send_tensix_risc_reset(CoreCoord, const TensixSoftResetOptions&) {}

void SWEmuleChip::send_tensix_risc_reset(const TensixSoftResetOptions&) {}

void SWEmuleChip::deassert_risc_resets() {}

void SWEmuleChip::set_power_state(DevicePowerState) {}

int SWEmuleChip::arc_msg(
    uint32_t, bool, const std::vector<uint32_t>&, const std::chrono::milliseconds, uint32_t* return_3, uint32_t*) {
    if (return_3) {
        *return_3 = 1;
    }
    return 0;
}

int SWEmuleChip::get_clock() { return 0; }

int SWEmuleChip::get_numa_node() { return 0; }

void SWEmuleChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>&) {}

void SWEmuleChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>&) {}

}  // namespace tt::umd
