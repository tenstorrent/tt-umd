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

/// SWEmuleChip extends Chip with real memory-backed I/O.
///
/// Worker L1 regions are allocated from a single contiguous L1Pool
/// (MAP_32BIT mmap with 2 MB aligned slots) for bitmask offset extraction.
/// DRAM cores use individual mmaps (not directly dereferenced by kernels).
/// All non-memory operations (barriers, resets, power management) are no-ops.
class SWEmuleChip : public Chip {
public:
    // chip_uid is the chip's GLOBALLY stable unique id, used to name the shared L1 segment under
    // TT_EMULE_CHIP_SHM. It must not be the ChipId: TT_VISIBLE_DEVICES makes the cluster descriptor
    // renumber each process's visible chips to 0..N-1, so two processes holding different physical
    // chips would both call theirs chip 0 and collide on one segment.
    //
    // Two overloads rather than a defaulted parameter, so the one-argument signature keeps compiling
    // for existing callers; a default argument is applied at the call site and would remove it.
    explicit SWEmuleChip(const SocDescriptor& soc_descriptor);
    // std::nullopt means "this chip has no stable identity", which is distinct from an id that
    // happens to be 0 -- the legacy descriptor computes chip << 32, so chip 0 has exactly that id.
    SWEmuleChip(const SocDescriptor& soc_descriptor, std::optional<uint64_t> chip_uid);
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

    // Pool slot for a worker core, or SIZE_MAX if it has none. The map is derived from the SoC
    // descriptor alone, so a chip owned by a PEER PROCESS with the same arch and harvesting has an
    // identical layout — which is what lets a rank resolve a NOC address into a chip it does not own,
    // from the peer's shared segment. See tt-emule docs/fabric-ccl-emulation.md.
    size_t slot_of(tt_xy_pair core_xy) const;

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

    // All cores (worker + DRAM), keyed by physical {x,y}.
    std::unordered_map<tt_xy_pair, std::unique_ptr<tt_emule::Core>> cores_;

    // One backing per DRAM channel — every NOC endpoint coord of a channel resolves
    // here (else a noc=1 access reads a different mmap than the noc=0 / host write).
    std::unordered_map<uint32_t, tt_emule::Core*> dram_channel_core_;
    std::vector<std::unique_ptr<tt_emule::Core>> dram_backings_;  // owns the per-channel mmaps

    uint32_t l1_size_;
    uint64_t dram_bank_size_;

    // Folded once in the ctor so the segment key and the peer-side lookup cannot drift apart.

    SocDescriptor soc_descriptor_;

    // Host-facing (PCIe) address resolution — an existing, already-upstream UMD class (also used
    // by TTSimTTDevice), not emule-specific. No host-memory channels to pre-reserve (SWEmuleChip
    // has no hugepage concept), so 0.
    std::unique_ptr<SimulationSysmemManager> sysmem_manager_;

    // NOTE: this is an ABI BREAK for emule builds. Members were removed and added, so both offsets
    // and sizeof(SWEmuleChip) change, and a client that allocated the old size cannot be reused --
    // rebuild anything linking it. The one-argument constructor below is kept for SOURCE
    // compatibility only. emule is an opt-in component (TT_UMD_BUILD_EMULE) with no ABI promise.
    std::unordered_map<tt_xy_pair, size_t> slot_of_;
    uint64_t shm_harvest_mask_ = 0;
};

}  // namespace tt::umd
