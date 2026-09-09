// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "umd/device/chip/chip.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt_emule {
class Core;
class L1Pool;
}  // namespace tt_emule

namespace tt::umd {

class SimulationSysmemManager;

// TRANSLATED Tensix core -> worker-pool slot, in the SoC descriptor's deterministic order.
//
// Free function, not a member: a rank resolving a fabric write into a chip owned by ANOTHER PROCESS
// has that chip's SocDescriptor but no SWEmuleChip for it, and must derive the same layout to index
// the peer's shared segment. One implementation, so the two sides cannot drift.
std::unordered_map<tt_xy_pair, size_t> build_worker_slot_map(const SocDescriptor& soc_descriptor);

/// SWEmuleChip extends Chip with real memory-backed I/O.
///
/// Worker L1 regions come from a single contiguous L1Pool carved into fixed-size slots, so an
/// in-slot offset is recoverable with one mask. Under TT_EMULE_CHIP_SHM that pool is a named
/// shared segment, and a slot index is then a portable identity a peer process can resolve.
/// DRAM cores use individual mmaps (not directly dereferenced by kernels).
/// All non-memory operations (barriers, resets, power management) are no-ops.
class SWEmuleChip : public Chip {
public:
    // chip_uid is the chip's GLOBALLY stable unique id, used to name the shared L1 segment under
    // TT_EMULE_CHIP_SHM. It must not be the ChipId: TT_VISIBLE_DEVICES makes the cluster descriptor
    // renumber each process's visible chips to 0..N-1, so two processes holding different physical
    // chips would both call theirs chip 0 and collide on one segment. std::nullopt means "no stable
    // identity", which is why the caller must not substitute a synthesized id: an id the descriptor
    // invented per-process is not shareable, and passing one would name a colliding segment.
    explicit SWEmuleChip(const SocDescriptor& soc_descriptor, std::optional<uint64_t> chip_uid = std::nullopt);
    ~SWEmuleChip() override;

    // Chip lifecycle — no-ops.
    bool is_mmio_capable() const override;
    void start_device(uint32_t dram_membar_subchannel = 0) override;
    void close_device() override;

    // Hardware accessors — not applicable.
    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    const SocDescriptor& get_soc_descriptor() const override { return soc_descriptor_; }

    // Host memory — no-ops.
    int get_num_host_channels() override;
    int get_host_channel_size(std::uint32_t channel) override;
    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    // Memory I/O — backed by tt_emule::Core storage.
    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) override;
    void write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) override;
    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;
    void noc_multicast_write(
        const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;

    // Barriers and resets — no-ops.
    void wait_for_non_mmio_flush() override;
    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel = 0) override;
    void deassert_risc_resets() override;
    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) override;
    int get_clock() override;
    int get_numa_node() override;
    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) override;

    // Public accessors.
    uint32_t l1_size() const { return l1_size_; }

    uint64_t dram_bank_size() const { return dram_bank_size_; }

    // Get the tt_emule::Core for a given worker core coordinate (lazy-create).
    tt_emule::Core* get_core(tt_xy_pair core_xy);

    // Get the single backing for a DRAM channel (lazy-create). Every NOC endpoint
    // coord of a channel resolves here, so a noc=1 read sees a noc=0 / host write.
    tt_emule::Core* get_dram_channel_backing(uint32_t channel);

    // Pool slot for a worker core in TRANSLATED naming, or nullopt if it has none.
    std::optional<size_t> slot_of(tt_xy_pair translated_core_xy) const;

    // Slots in the worker pool; with SLOT_SIZE this is the shared segment's exact size.
    size_t num_pool_slots() const;

    // Identity of this chip's shared segment: the harvesting mask folded exactly as the ctor folds it.
    uint64_t shm_harvest_mask() const;

private:
    std::mutex core_mutex_;

    // L1Pool for worker cores — one contiguous mmap carved into fixed-size slots.
    std::unique_ptr<tt_emule::L1Pool> worker_pool_;

    // Tensix core → pool slot, built ONCE from the SoC descriptor rather than on first touch.
    // Touch order differs between a process that dispatches kernels (grid walk) and one that only
    // host-writes buffers (write order), so a first-touch counter gives the same core a different
    // slot in each — invisible today, silent cross-process corruption once the pool is shared.
    std::unordered_map<tt_xy_pair, size_t> slot_of_;

    // Folded once in the ctor so the segment key and the peer-side lookup cannot drift apart.
    uint64_t shm_harvest_mask_ = 0;

    // All cores (worker + DRAM), keyed by physical {x,y}.
    std::unordered_map<tt_xy_pair, std::unique_ptr<tt_emule::Core>> cores_;

    // One backing per DRAM channel — every NOC endpoint coord of a channel resolves
    // here (else a noc=1 access reads a different mmap than the noc=0 / host write).
    std::unordered_map<uint32_t, tt_emule::Core*> dram_channel_core_;
    std::vector<std::unique_ptr<tt_emule::Core>> dram_backings_;  // owns the per-channel mmaps

    uint32_t l1_size_;
    uint64_t dram_bank_size_;

    SocDescriptor soc_descriptor_;

    // Host-facing (PCIe) address resolution — an existing, already-upstream UMD class (also used
    // by TTSimTTDevice), not emule-specific. No host-memory channels to pre-reserve (SWEmuleChip
    // has no hugepage concept), so 0.
    std::unique_ptr<SimulationSysmemManager> sysmem_manager_;
};

}  // namespace tt::umd
