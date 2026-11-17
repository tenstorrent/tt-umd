// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd-workload/chip/sw_emule_chip.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <utility>
#include <vector>

#include "tt-umd/chip_helpers/simulation_sysmem_manager.hpp"
#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt_device_model/simulation_tt_device_model.hpp"
#include "tt_emule/device.hpp"
#include "tt_emule/l1_pool.hpp"

namespace tt::umd {

namespace {
// Empty/unset and "0" disable sharing; any other nonempty value requests it.
// Keep private pools usable with the declared tt-emule pin, which has no shared-pool API.
bool shared_pool_requested() {
    const char* value = std::getenv("TT_EMULE_CHIP_SHM");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

template <typename Pool>
std::unique_ptr<Pool> make_shared_pool(size_t slots, uint64_t uid, uint64_t harvest_mask) {
    if constexpr (std::is_constructible_v<Pool, size_t, uint64_t, uint64_t>) {
        return std::make_unique<Pool>(slots, uid, harvest_mask);
    } else {
        UMD_THROW(error::RuntimeError, "SWEmuleChip: this tt-emule dependency does not support TT_EMULE_CHIP_SHM");
    }
}

// A tt-emule that sizes the pool's slots from the worker L1 takes the size; an older one has fixed slots.
template <typename Pool>
std::unique_ptr<Pool> make_private_pool(size_t slots, size_t worker_l1_size) {
    if constexpr (std::is_constructible_v<Pool, size_t, size_t>) {
        return std::make_unique<Pool>(slots, worker_l1_size);
    } else {
        return std::make_unique<Pool>(slots);
    }
}

template <typename Pool>
std::unique_ptr<Pool> make_sized_shared_pool(size_t slots, size_t worker_l1_size, uint64_t uid, uint64_t harvest_mask) {
    if constexpr (std::is_constructible_v<Pool, size_t, size_t, uint64_t, uint64_t>) {
        return std::make_unique<Pool>(slots, worker_l1_size, uid, harvest_mask);
    } else {
        return make_shared_pool<Pool>(slots, uid, harvest_mask);
    }
}

template <typename Pool, typename = void>
struct HasSlotSize : std::false_type {};

template <typename Pool>
struct HasSlotSize<Pool, std::void_t<decltype(std::declval<const Pool&>().slot_size())>> : std::true_type {};

template <typename Pool>
size_t pool_slot_size(const Pool& pool) {
    if constexpr (HasSlotSize<Pool>::value) {
        return pool.slot_size();
    } else {
        return Pool::SLOT_SIZE;
    }
}

// A tt-emule core that observes host writes (a kernel parked on a word the host stores) gets told of each one.
template <typename Core>
auto notify_host_write(Core& core, uint64_t addr, size_t size, int) -> decltype(core.notify_host_write(addr, size)) {
    return core.notify_host_write(addr, size);
}

template <typename Core>
void notify_host_write(Core&, uint64_t, size_t, long) {}

template <typename Core>
auto notify_host_read(Core& core, uint64_t addr, size_t size, int) -> decltype(core.notify_host_read(addr, size)) {
    return core.notify_host_read(addr, size);
}

template <typename Core>
void notify_host_read(Core&, uint64_t, size_t, long) {}

// A host poll of the chip's host memory (a command-queue pointer) lets a tt-emule run make progress, as the
// simulator's clock does.
template <typename Core>
auto notify_host_poll(int) -> decltype(Core::notify_host_poll()) {
    return Core::notify_host_poll();
}

template <typename Core>
void notify_host_poll(long) {}

template <typename Pool, typename = void>
struct HasSharedPoolQuery : std::false_type {};

template <typename Pool>
struct HasSharedPoolQuery<Pool, std::void_t<decltype(std::declval<const Pool&>().is_shared())>> : std::true_type {};

template <typename Pool>
bool is_shared_pool(const Pool& pool) {
    if constexpr (HasSharedPoolQuery<Pool>::value) {
        return pool.is_shared();
    } else {
        return false;
    }
}
}  // namespace

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

namespace {

// The simulation device model (no management firmware behind it), with the chip's own windows onto its cores.
class SWEmuleTTDeviceModel final : public SimulationTTDeviceModel {
public:
    explicit SWEmuleTTDeviceModel(SWEmuleChip& chip) : SimulationTTDeviceModel(chip.get_soc_descriptor()), chip_(chip) {}

    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) override {
        return chip_.create_io_window(target, host);
    }

private:
    SWEmuleChip& chip_;
};

// A simulated chip's TTDevice, as TTSim chips have one, with the chip's own memory for every access.
class SWEmuleTTDevice final : public TTDevice {
public:
    explicit SWEmuleTTDevice(SWEmuleChip& chip) : TTDevice(std::make_unique<SWEmuleTTDeviceModel>(chip)), chip_(chip) {
        set_soc_descriptor(chip.get_soc_descriptor());
    }

    void read_from_device(void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId) override {
        chip_.read_from_device(core, mem_ptr, addr, size);
    }

    void write_to_device(const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId) override {
        chip_.write_to_device(core, mem_ptr, addr, size);
    }

    void advance_device_execution() override { notify_host_poll<tt_emule::Core>(0); }

private:
    SWEmuleChip& chip_;
};

}  // namespace

// Out-of-line destructor — tt_emule::Core and L1Pool must be complete for unique_ptr destruction.
SWEmuleChip::~SWEmuleChip() = default;

SWEmuleChip::SWEmuleChip(
    const SocDescriptor& soc_descriptor, std::optional<uint64_t> chip_uid, uint32_t num_host_mem_channels) :
    Chip(soc_descriptor.arch),
    soc_descriptor_(soc_descriptor),
    sysmem_manager_(std::make_unique<SimulationSysmemManager>(num_host_mem_channels, soc_descriptor.arch)) {
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
    if (chip_uid.has_value() && shared_pool_requested()) {
        worker_pool_ = make_sized_shared_pool<tt_emule::L1Pool>(
            pool_size, static_cast<size_t>(l1_size_), *chip_uid, shm_harvest_mask_);
    } else {
        // Refuse when sharing was ASKED FOR but this chip has no stable id. A process-private pool
        // is invisible to every peer, so the run would continue and drop each cross-rank write, and
        // the only symptom appears far away as a fabric problem. Fail at the cause instead.
        if (!chip_uid.has_value() && shared_pool_requested()) {
            UMD_THROW(
                error::RuntimeError,
                "SWEmuleChip: TT_EMULE_CHIP_SHM requests shared chip backing, but this chip has no "
                "unique id in the cluster descriptor; a peer rank could never attach its L1");
        }
        worker_pool_ = make_private_pool<tt_emule::L1Pool>(pool_size, static_cast<size_t>(l1_size_));
    }
    // A slot must hold a whole core's L1. A descriptor whose worker_l1_size exceeds the slot stride would
    // silently overrun into the next core's slot, so refuse it at construction.
    if (static_cast<size_t>(l1_size_) > pool_slot_size(*worker_pool_)) {
        UMD_THROW(
            error::RuntimeError,
            "SWEmuleChip: worker_l1_size " + std::to_string(l1_size_) + " exceeds the L1Pool slot stride " +
                std::to_string(pool_slot_size(*worker_pool_)) + " — adjacent cores' L1 would overlap");
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
        if (worker_pool_ && is_shared_pool(*worker_pool_)) {
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
void SWEmuleChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size, IoOrdering ordering) {
    tt_emule::Core* target_core =
        (core.core_type == CoreType::DRAM)
            ? get_dram_channel_backing(
                  static_cast<uint32_t>(get_soc_descriptor().get_dram_channel_for_core(core).first))
            : get_core(get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id()));
    target_core->access(l1_dest, const_cast<void*>(src), size, /*is_write=*/true);
    if (l1_dest < target_core->l1_size()) {
        notify_host_write(*target_core, l1_dest, size, 0);
    }
}

void SWEmuleChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size, IoOrdering ordering) {
    tt_emule::Core* target_core =
        (core.core_type == CoreType::DRAM)
            ? get_dram_channel_backing(
                  static_cast<uint32_t>(get_soc_descriptor().get_dram_channel_for_core(core).first))
            : get_core(get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id()));
    // The read first lets work the device is still running reach the state silicon would have by now.
    notify_host_read(*target_core, l1_src, size, 0);
    target_core->access(l1_src, dest, size, /*is_write=*/false);
}

// Register I/O takes the same path: an address past the core's memory reaches tt-emule's register model.
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

namespace {

// IoWindow over an emulated core. A store is one write_to_device, so a 32-bit store lands whole and
// the core sees it as a host write; there is no mapping to size, so the window spans the request.
class SWEmuleIoWindow final : public IoWindow {
public:
    SWEmuleIoWindow(SWEmuleChip& chip, const TargetIoWindowConfig& target, const HostIoWindowConfig& host) :
        chip_(chip), host_(host) {
        configure(target);
    }

    void write_block(uint64_t offset, const void* data, size_t size) override { write(offset, data, size); }
    void read_block(uint64_t offset, void* data, size_t size) override { read(offset, data, size); }
    void write16(uint64_t offset, uint16_t value) override { write(offset, &value, sizeof(value)); }
    uint16_t read16(uint64_t offset) override { return load<uint16_t>(offset); }
    void write32(uint64_t offset, uint32_t value) override { write(offset, &value, sizeof(value)); }
    uint32_t read32(uint64_t offset) override { return load<uint32_t>(offset); }
    void write64(uint64_t offset, uint64_t value) override { write(offset, &value, sizeof(value)); }
    uint64_t read64(uint64_t offset) override { return load<uint64_t>(offset); }
    void write_aligned(uint64_t offset, const void* data, size_t size) override { write(offset, data, size); }
    void read_aligned(uint64_t offset, void* data, size_t size) override { read(offset, data, size); }

    void configure(const TargetIoWindowConfig& config) override { configure(config, IoOrdering::Strict); }

    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override {
        UMD_ASSERT(
            !config.core_end.has_value(), error::RuntimeError, "SWEmuleChip: multicast IoWindows are not supported");
        target_ = config;
        ordering_ = ordering;
        core_ = chip_.get_soc_descriptor().get_coord_at(config.core_start, CoordSystem::TRANSLATED);
    }

    TargetIoWindowConfig get_target_config() const override { return target_; }
    IoOrdering get_io_ordering() const override { return ordering_; }
    size_t get_size() const override { return host_.size; }
    HostMemoryCaching get_memory_caching_type() const override { return host_.mapping; }

private:
    // As a TLB window: an access past the window's end faults instead of reaching the memory beyond it.
    void validate(uint64_t offset, size_t size) const {
        if (offset + size > host_.size) {
            throw std::out_of_range("Out of bounds access");
        }
    }
    void write(uint64_t offset, const void* data, size_t size) {
        validate(offset, size);
        chip_.write_to_device(core_, data, target_.addr + offset, size);
    }
    void read(uint64_t offset, void* data, size_t size) {
        validate(offset, size);
        chip_.read_from_device(core_, data, target_.addr + offset, size);
    }
    template <typename T>
    T load(uint64_t offset) {
        T value{};
        read(offset, &value, sizeof(value));
        return value;
    }

    SWEmuleChip& chip_;
    HostIoWindowConfig host_;
    TargetIoWindowConfig target_{};
    IoOrdering ordering_ = IoOrdering::Strict;
    CoreCoord core_;
};

}  // namespace

std::unique_ptr<IoWindow> SWEmuleChip::create_io_window(
    const TargetIoWindowConfig& target, const HostIoWindowConfig& host) {
    return std::make_unique<SWEmuleIoWindow>(*this, target, host);
}

// --- Chip lifecycle / hardware accessors (no-ops) ---

bool SWEmuleChip::is_mmio_capable() const { return false; }

void SWEmuleChip::start_device(uint32_t) {}

void SWEmuleChip::close_device() {}

TTDevice* SWEmuleChip::get_tt_device() {
    if (!tt_device_) {
        tt_device_ = std::make_unique<SWEmuleTTDevice>(*this);
    }
    return tt_device_.get();
}

SysmemManager* SWEmuleChip::get_sysmem_manager() { return sysmem_manager_.get(); }

// --- Host memory: the cluster's host channels, as a simulated chip has them ---

int SWEmuleChip::get_num_host_channels() { return static_cast<int>(sysmem_manager_->get_num_host_mem_channels()); }

int SWEmuleChip::get_host_channel_size(std::uint32_t channel) {
    UMD_ASSERT(
        channel < static_cast<uint32_t>(get_num_host_channels()),
        error::RuntimeError,
        "Querying size for a host channel that does not exist.");
    return static_cast<int>(sysmem_manager_->get_hugepage_mapping(channel).mapping_size);
}

void SWEmuleChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    sysmem_manager_->write_to_sysmem(channel, src, sysmem_dest, size);
}

void SWEmuleChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    sysmem_manager_->read_from_sysmem(channel, dest, sysmem_src, size);
}

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
