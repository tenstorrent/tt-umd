// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/sw_emule_chip.hpp"

#include <cassert>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "tt_emule/device.hpp"
#include "tt_emule/l1_pool.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::unordered_map<tt_xy_pair, size_t> build_worker_slot_map(const SocDescriptor& soc_descriptor) {
    // TRANSLATED, because that is the naming get_core() resolves in: write_to_device normalizes
    // every incoming CoreCoord to it before looking a core up. The descriptor's core order is a
    // deterministic walk, so this map is identical in every process given the same descriptor and
    // harvesting mask -- which is what makes a slot index a portable identity.
    const auto tensix = soc_descriptor.get_cores(tt::CoreType::TENSIX, CoordSystem::TRANSLATED);
    std::unordered_map<tt_xy_pair, size_t> slot_of;
    slot_of.reserve(tensix.size());
    for (size_t i = 0; i < tensix.size(); ++i) {
        slot_of[tt_xy_pair(tensix[i].x, tensix[i].y)] = i;
    }
    return slot_of;
}

// Out-of-line destructor — tt_emule::Core and L1Pool must be complete for unique_ptr destruction.
SWEmuleChip::~SWEmuleChip() = default;

SWEmuleChip::SWEmuleChip(const SocDescriptor& soc_descriptor, std::optional<uint64_t> chip_uid) :
    Chip(soc_descriptor.arch),
    soc_descriptor_(soc_descriptor),
    sysmem_manager_(std::make_unique<SimulationSysmemManager>(/*num_host_mem_channels=*/0, soc_descriptor.arch)) {
    auto& soc = get_soc_descriptor();

    l1_size_ = soc.worker_l1_size;
    // Use full DRAM bank size — DRAM cores use regular mmap (not MAP_32BIT),
    // so virtual address space is not constrained.  Wormhole views use address
    // offsets up to 1 GB within a 2 GB bank, so capping below bank size causes
    // writes to segfault.  Overcommit means only touched pages use physical RAM.
    dram_bank_size_ = soc.dram_bank_size;
    // A real on-chip DRAM offset can't structurally exceed its own bank's capacity, so it can
    // never reach __emule_resolve_noc_addr's pcie_base_ threshold (which routes >= addresses to
    // SimulationSysmemManager instead of the core map) — documents that margin rather than
    // resting on it implicitly.
    assert(dram_bank_size_ < SysmemManager::get_pcie_base_for_arch(soc_descriptor.arch));

    // One slot per Tensix core, and only WORKER cores use the pool, so num_tensix is an exact
    // bound. No fallback for an empty core list: nothing could index those slots, and the pool
    // sits in the scarce low-4 GB MAP_32BIT window where over-sizing costs mesh capacity.
    slot_of_ = build_worker_slot_map(soc);
    const size_t pool_size = slot_of_.size();
    // A slot must hold a whole core's L1. A descriptor whose worker_l1_size exceeds the slot stride
    // (e.g. tests/soc_descs/quasar_simulation_1x1.yaml, 4 MiB) would silently overrun into the next
    // core's slot, so refuse it at construction.
    if (static_cast<size_t>(l1_size_) > tt_emule::L1Pool::SLOT_SIZE) {
        UMD_THROW(
            error::RuntimeError,
            "SWEmuleChip: worker_l1_size " + std::to_string(l1_size_) + " exceeds the L1Pool slot stride " +
                std::to_string(tt_emule::L1Pool::SLOT_SIZE) + " — adjacent cores' L1 would overlap");
    }
    // Shared backing needs a stable identity; without a uid we can only be process-private.
    //
    // The harvesting masks join the key because a PEER reconstructs this chip's SocDescriptor from
    // the segment name alone, and that reconstruction must produce a *constructible* descriptor:
    // Blackhole rejects an eth mask harvesting neither 2 nor 14 channels, so a name carrying only
    // the tensix mask makes every peer rebuild throw and every cross-rank delivery vanish. The
    // masks are not here because they move the slot layout -- only the tensix mask does that.
    //
    // Packed into disjoint 20-bit fields, never XOR-folded: folding overlapping shifts is not
    // injective, so two chips with genuinely different core lists could collide on one key, attach
    // to each other's segment and resolve through the wrong slot map. 20 bits is far above any real
    // per-chip mask; a wider one would alias, so refuse it rather than corrupt silently.
    //
    // pcie/l2cpu are omitted because five 20-bit fields do not fit a 64-bit name component. That is
    // safe while they cannot change the worker list -- verified on Blackhole, where dropping a
    // non-zero pcie mask yields an identical translated Tensix core list. Revisit if that changes.
    {
        const auto& hm = soc.harvesting_masks;
        constexpr uint64_t HARVEST_FIELD_BITS = 20;
        constexpr uint64_t HARVEST_FIELD_MASK = (1ull << HARVEST_FIELD_BITS) - 1;
        const uint64_t tensix_mask = hm.tensix_harvesting_mask;
        const uint64_t dram_mask = hm.dram_harvesting_mask;
        const uint64_t eth_mask = hm.eth_harvesting_mask;
        if (tensix_mask > HARVEST_FIELD_MASK || dram_mask > HARVEST_FIELD_MASK || eth_mask > HARVEST_FIELD_MASK) {
            UMD_THROW(
                error::RuntimeError,
                "SWEmuleChip: harvesting mask exceeds the " + std::to_string(HARVEST_FIELD_BITS) +
                    "-bit field width of the shared-segment key; widen the key before sharing this chip");
        }
        shm_harvest_mask_ = tensix_mask | (dram_mask << HARVEST_FIELD_BITS) | (eth_mask << (2 * HARVEST_FIELD_BITS));
    }
    if (chip_uid.has_value() && tt_emule::chip_store_shared()) {
        worker_pool_ = std::make_unique<tt_emule::L1Pool>(pool_size, *chip_uid, shm_harvest_mask_);
    } else {
        // Refuse when sharing was ASKED FOR but this chip has no stable id. A process-private pool
        // is invisible to every peer, so the run would continue and drop each cross-rank write, and
        // the only symptom appears far away as a fabric problem. Fail at the cause instead.
        if (!chip_uid.has_value() && tt_emule::chip_store_shared()) {
            UMD_THROW(
                error::RuntimeError,
                "SWEmuleChip: TT_EMULE_CHIP_SHM requests shared chip backing, but this chip has no "
                "unique id in the cluster descriptor; a peer rank could never attach its L1");
        }
        worker_pool_ = std::make_unique<tt_emule::L1Pool>(pool_size);
    }
}

std::optional<size_t> SWEmuleChip::slot_of(tt_xy_pair translated_core_xy) const {
    auto it = slot_of_.find(translated_core_xy);
    return (it == slot_of_.end()) ? std::nullopt : std::optional<size_t>(it->second);
}

size_t SWEmuleChip::num_pool_slots() const { return worker_pool_ ? worker_pool_->num_slots() : 0; }

uint64_t SWEmuleChip::shm_harvest_mask() const { return shm_harvest_mask_; }

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

    // Keyed on the TRANSLATED (x,y) the entry points normalize to — a tile's names share one L1 on
    // silicon, so two encodings for the same worker must not split it into two Core backings.
    auto it = cores_.find(core_xy);
    if (it != cores_.end()) {
        return it->second.get();
    }

    // Lazy-create the Core object, but at its PRE-ASSIGNED slot — the backing address is a property
    // of the core's identity, not of when it was first touched.
    tt_emule::CoreCoord coord{core_xy.x, core_xy.y};
    std::unique_ptr<tt_emule::Core> core;
    auto slot = slot_of_.find(core_xy);
    if (slot != slot_of_.end()) {
        // Defensive: a shared pool's slot count comes from the segment, so a peer that computed a
        // different layout under the same key would put this index out of range. slot_ptr answers
        // null there, and the memcpy below would fault with nothing said.
        UMD_ASSERT(
            slot->second < worker_pool_->num_slots(),
            error::RuntimeError,
            "SWEmuleChip: slot " + std::to_string(slot->second) + " for core (" + std::to_string(core_xy.x) + "," +
                std::to_string(core_xy.y) + ") is outside the " + std::to_string(worker_pool_->num_slots()) +
                "-slot worker pool");
        core = std::make_unique<tt_emule::Core>(
            coord, worker_pool_->slot_ptr(slot->second), static_cast<size_t>(l1_size_));
    } else {
        // No slot: a non-pooled core, which is expected traffic — write_to_device routes
        // ETH/PCIE/DISPATCH/ARC here too, since it only special-cases DRAM. Only a SHARED pool
        // makes this actionable, because the private mapping is then invisible to peer ranks.
        if (worker_pool_ && worker_pool_->is_shared()) {
            static std::once_flag warned;
            std::call_once(warned, [&]() {
                log_warning(
                    LogUMD,
                    "{} core ({},{}) has no slot in the SHARED worker pool; backing it with a private "
                    "mmap that peer ranks cannot see (warned once)",
                    tt::arch_to_str(soc_descriptor_.arch),
                    core_xy.x,
                    core_xy.y);
            });
        } else {
            log_debug(
                LogUMD,
                "core ({},{}) is not a pooled Tensix core; backing it with an individual mmap",
                core_xy.x,
                core_xy.y);
        }
        core = std::make_unique<tt_emule::Core>(coord, tt_emule::CoreRole::WORKER, static_cast<size_t>(l1_size_));
    }

    tt_emule::Core* raw_ptr = core.get();
    cores_[core_xy] = std::move(core);
    return raw_ptr;
}

// Normalize to TRANSLATED before resolving a worker, as LocalChip does: Cluster forwards the
// caller's CoreCoord untranslated, so a raw {x,y} would otherwise be looked up in whichever naming
// the caller happened to pick, and only TRANSLATED has a pool slot. A LITERAL coord passes through
// unchanged, which is the pre-existing behaviour for callers that already resolved the coordinate.
void SWEmuleChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) {
    tt_emule::Core* target_core =
        (core.core_type == CoreType::DRAM)
            ? get_dram_channel_backing(
                  static_cast<uint32_t>(get_soc_descriptor().get_dram_channel_for_core(core).first))
            : get_core(get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id()));
    std::memcpy(target_core->l1_ptr(l1_dest), src, size);
}

void SWEmuleChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) {
    tt_emule::Core* target_core =
        (core.core_type == CoreType::DRAM)
            ? get_dram_channel_backing(
                  static_cast<uint32_t>(get_soc_descriptor().get_dram_channel_for_core(core).first))
            : get_core(get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id()));
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

SysmemManager* SWEmuleChip::get_sysmem_manager() { return sysmem_manager_.get(); }

// --- Host memory (no-ops) ---

int SWEmuleChip::get_num_host_channels() { return 0; }

int SWEmuleChip::get_host_channel_size(std::uint32_t) { return 0; }

void SWEmuleChip::write_to_sysmem(uint16_t, const void*, uint64_t, uint32_t) {}

void SWEmuleChip::read_from_sysmem(uint16_t, void*, uint64_t, uint32_t) {}

// --- Multicast (not implemented) ---

void SWEmuleChip::dma_multicast_write(void*, size_t, CoreCoord, CoreCoord, uint64_t) {
    UMD_THROW(error::RuntimeError, "SWEmuleChip::dma_multicast_write is not implemented");
}

void SWEmuleChip::noc_multicast_write(const void*, size_t, CoreCoord, CoreCoord, uint64_t) {
    UMD_THROW(error::RuntimeError, "SWEmuleChip::noc_multicast_write is not implemented");
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
