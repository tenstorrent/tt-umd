// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/sw_emule_chip.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "tt_emule/device.hpp"
#include "tt_emule/l1_pool.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt::umd {

// Out-of-line destructor — tt_emule::Core and L1Pool must be complete for unique_ptr destruction.
SWEmuleChip::~SWEmuleChip() = default;

SWEmuleChip::SWEmuleChip(const SocDescriptor& soc_descriptor) :
    Chip(soc_descriptor.arch), soc_descriptor_(soc_descriptor) {
    auto& soc = get_soc_descriptor();

    l1_size_ = soc.worker_l1_size;
    // Use full DRAM bank size — DRAM cores use regular mmap (not MAP_32BIT),
    // so virtual address space is not constrained.  Wormhole views use address
    // offsets up to 1 GB within a 2 GB bank, so capping below bank size causes
    // writes to segfault.  Overcommit means only touched pages use physical RAM.
    dram_bank_size_ = soc.dram_bank_size;

    // Allocate L1Pool for worker cores.
    // Use a generous count covering Tensix + Ethernet + Router + other non-DRAM cores,
    // since all non-DRAM cores go through the pool for consistent bitmask offset extraction.
    // Add extra headroom for cores created via translated coords that differ from physical coords.
    size_t num_tensix = soc.get_cores(tt::CoreType::TENSIX).size();
    // 128 is a safe upper bound on Tensix cores across known architectures (Wormhole=72,
    // Blackhole~120). Used as fallback if SOC descriptor reports zero.
    size_t pool_size = (num_tensix > 0 ? num_tensix : 128) * 2;  // 2× headroom
    worker_pool_ = std::make_unique<tt_emule::L1Pool>(pool_size);
}

// One physical backing per DRAM CHANNEL. A channel is fronted by several NOC endpoint
// coords (per-NOC preferred workers / subchannels) that all address the same bank on
// silicon, so the host (NOC0/TRANSLATED) and a noc=1 kernel read must land on the same
// Core. Callers resolve the channel from the tagged CoreCoord (host) or the loop index
// (runner) via SocDescriptor's LOGICAL mapping; here we just alias the channel to one
// mmap (individual, not pooled, not MAP_32BIT).
tt_emule::Core* SWEmuleChip::get_dram_channel_backing(uint32_t channel) {
    std::lock_guard<std::mutex> lock(core_mutex_);
    auto it = dram_channel_core_.find(channel);
    if (it != dram_channel_core_.end()) {
        return it->second;
    }
    auto dram_core = std::make_unique<tt_emule::Core>(
        tt_emule::CoreCoord{channel, 0}, tt_emule::CoreRole::DRAM, static_cast<size_t>(dram_bank_size_));
    tt_emule::Core* raw_ptr = dram_core.get();
    dram_channel_core_[channel] = raw_ptr;
    dram_backings_.push_back(std::move(dram_core));
    return raw_ptr;
}

tt_emule::Core* SWEmuleChip::get_core(tt_xy_pair core_xy) {
    std::lock_guard<std::mutex> lock(core_mutex_);

    auto it = cores_.find(core_xy);
    if (it != cores_.end()) {
        return it->second.get();
    }

    // Lazy-create worker core.
    tt_emule::CoreCoord coord{core_xy.x, core_xy.y};
    std::unique_ptr<tt_emule::Core> core;
    if (next_slot_ < worker_pool_->num_slots()) {
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
    return raw_ptr;
}

void SWEmuleChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) {
    tt_emule::Core* target_core = (core.core_type == CoreType::DRAM)
                                      ? get_dram_channel_backing(static_cast<uint32_t>(
                                            get_soc_descriptor().get_dram_channel_for_core(core).first))
                                      : get_core(tt_xy_pair(core.x, core.y));
    std::memcpy(target_core->l1_ptr(l1_dest), src, size);
}

void SWEmuleChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) {
    tt_emule::Core* target_core = (core.core_type == CoreType::DRAM)
                                      ? get_dram_channel_backing(static_cast<uint32_t>(
                                            get_soc_descriptor().get_dram_channel_for_core(core).first))
                                      : get_core(tt_xy_pair(core.x, core.y));
    std::memcpy(dest, target_core->l1_ptr(l1_src), size);
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

void SWEmuleChip::start_device(uint32_t) {}

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

void SWEmuleChip::noc_multicast_write(const void*, size_t, CoreCoord, CoreCoord, uint64_t) {
    throw std::runtime_error("SWEmuleChip::noc_multicast_write is not implemented");
}

// --- Barriers, resets, power (no-ops) ---

void SWEmuleChip::wait_for_non_mmio_flush() {}

void SWEmuleChip::l1_membar(const std::unordered_set<CoreCoord>&) {}

void SWEmuleChip::dram_membar(const std::unordered_set<CoreCoord>&) {}

void SWEmuleChip::dram_membar(const std::unordered_set<uint32_t>&, uint32_t) {}

void SWEmuleChip::deassert_risc_resets() {}

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
