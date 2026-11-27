// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd-workload/chip/local_chip.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "pcie/io_window_reconfigure.hpp"
#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/driver_atomics.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/system_memory/silicon_sysmem_manager.hpp"
#include "tt-umd/system_memory/sysmem_buffer.hpp"
#include "tt-umd/system_memory/sysmem_manager.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_types.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/types/host_memory.hpp"
#include "tt-umd/types/risc_type.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/timeouts.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<LocalChip>(), "LocalChip must be non-abstract.");

namespace {

// Don't use the top 256MB of the 4th host memory channel on WH.  Two reasons:
// 1. There are PCIE PHY registers at the top
// 2. Provision for platform software to have NOC-accessible host memory
constexpr size_t WORMHOLE_CHANNEL_3_SIZE_LIMIT = 768 * (1 << 20);

size_t get_host_mem_channel_size(tt::ARCH arch, size_t channel) {
    return (arch == tt::ARCH::WORMHOLE_B0 && channel == 3) ? WORMHOLE_CHANNEL_3_SIZE_LIMIT
                                                           : HostMemChannels::CHANNEL_STRIDE;
}

// Channels sit CHANNEL_STRIDE apart, so one buffer backing the first num_channels spans this much.
size_t get_host_mem_channels_span(tt::ARCH arch, size_t num_channels) {
    return (num_channels - 1) * HostMemChannels::CHANNEL_STRIDE + get_host_mem_channel_size(arch, num_channels - 1);
}

struct HostMemChannelPlan {
    uint32_t num_channels = 0;
    // One buffer for all channels when the allocator can hold one that spans them, since each NOC-bound buffer takes
    // one of the 16 outbound iATU regions every process on the device shares. Otherwise a limited allocator backs each
    // channel with its own hugepage, so it gets one buffer per channel.
    bool single_buffer = false;
};

HostMemChannelPlan plan_host_mem_channels(
    const SystemMemoryAllocator& allocator, tt::ARCH arch, uint32_t requested_channels, int device_id) {
    UMD_ASSERT(
        requested_channels <= HostMemChannels::MAX_CHANNELS,
        error::RuntimeError,
        fmt::format(
            "Only {} host memory channels are supported per device, but {} requested.",
            HostMemChannels::MAX_CHANNELS,
            requested_channels));
    if (requested_channels == 0) {
        return {};
    }
    const size_t span = get_host_mem_channels_span(arch, requested_channels);
    const std::optional<size_t> max_spanning_buffers = allocator.get_max_buffer_count(span);
    if (!max_spanning_buffers.has_value() || *max_spanning_buffers > 0) {
        return {requested_channels, /*single_buffer=*/true};
    }
    // A single channel spans exactly one stride, so its count is already known; asking again repeats the allocator's
    // warnings.
    const std::optional<size_t> max_channels = span == HostMemChannels::CHANNEL_STRIDE
                                                   ? max_spanning_buffers
                                                   : allocator.get_max_buffer_count(HostMemChannels::CHANNEL_STRIDE);
    // TODO: get rid of this when the following Metal CI issue is resolved.
    // https://github.com/tenstorrent/tt-metal/issues/15675
    // The notion that we should clamp the number of host mem channels to
    // what we have available and emit a warning is wrong, since the
    // application might try to use the channels it asked for.  We should
    // just fail early since the error message will be actionable instead of
    // a segfault or memory corruption.
    const auto num_channels =
        static_cast<uint32_t>(std::min<size_t>(requested_channels, max_channels.value_or(requested_channels)));
    if (num_channels < requested_channels) {
        log_warning(
            LogUMD,
            "NumHostMemChannels: {} used for device {} less than target: {}. Workload will fail if it exceeds "
            "NumHostMemChannels. Increase the host memory available to the device, such as the number of hugepages.",
            num_channels,
            device_id,
            requested_channels);
    }
    return {num_channels, /*single_buffer=*/false};
}

// The buffer's IOVA is valid only for the device it was pinned for, and the DMA engine trusts whatever range it is
// given, so both are checked before a transfer starts.
void check_zero_copy_buffer(const SysmemBuffer& buffer, int device_id, size_t offset, size_t size) {
    UMD_ASSERT(
        buffer.get_communication_id() == device_id,
        error::RuntimeError,
        fmt::format(
            "Sysmem buffer was pinned for device {}, not for device {}.", buffer.get_communication_id(), device_id));
    UMD_ASSERT(
        offset < buffer.get_size() && size <= buffer.get_size() - offset,
        error::RuntimeError,
        fmt::format(
            "Range starting at {:#x} with size {:#x} is out of bounds for sysmem buffer of size {:#x}.",
            offset,
            size,
            buffer.get_size()));
}

// Names the channels one buffer backs, so a message about the buffer points at the memory it actually covers.
std::string describe_host_mem_channels(size_t first_channel, size_t num_channels) {
    if (num_channels == 1) {
        return fmt::format("channel {}", first_channel);
    }
    return fmt::format("channels {}-{}", first_channel, first_channel + num_channels - 1);
}

// Device code reaches channel N at noc_base + N * CHANNEL_STRIDE, and the NOC-to-host window is shared by every
// process on the device, so a channel bound anywhere else would let the device reach another process's memory.
void check_channel_noc_address(
    const std::optional<uint64_t>& noc_address,
    uint64_t expected_noc_address,
    const std::string& channels,
    int device_id) {
    UMD_ASSERT(
        noc_address.has_value(),
        error::RuntimeError,
        fmt::format("Host memory for {} of device {} is not bound to the NOC.", channels, device_id));
    UMD_ASSERT(
        *noc_address == expected_noc_address,
        error::RuntimeError,
        fmt::format(
            "Host memory for {} of device {} was mapped at NOC address {:#x} instead of {:#x}. This usually means "
            "another process (often a stale or crashed one from a previous run) holds the sysmem NOC address space UMD "
            "requires; find and kill the other processes using the Tenstorrent device(s), then retry.",
            channels,
            device_id,
            *noc_address,
            expected_noc_address));
}

// Allocates the buffer backing num_channels channels starting at first_channel, and checks it landed where those
// channels are expected on the NOC.
std::unique_ptr<SysmemBuffer> allocate_host_mem_buffer(
    const std::function<std::unique_ptr<SysmemBuffer>()>& allocate,
    uint64_t expected_noc_address,
    size_t first_channel,
    size_t num_channels,
    int device_id) {
    const std::string channels = describe_host_mem_channels(first_channel, num_channels);
    std::unique_ptr<SysmemBuffer> buffer;
    try {
        buffer = allocate();
    } catch (const std::exception& e) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to allocate host memory {} for device {}: {}", channels, device_id, e.what()));
    }
    check_channel_noc_address(buffer->get_noc_address(), expected_noc_address, channels, device_id);
    log_debug(
        LogUMD,
        "Mapped host memory {} at IOVA {:#x} to NOC address {:#x}",
        channels,
        buffer->get_iova(),
        *buffer->get_noc_address());
    return buffer;
}

}  // namespace

std::unique_ptr<LocalChip> LocalChip::create(std::unique_ptr<TTDevice> tt_device, int num_host_mem_channels) {
    HostMemChannelPlan channel_plan;

    if (tt_device == nullptr) {
        UMD_THROW(error::RuntimeError, "Cannot create LocalChip without a TTDevice.");
    }

    if (SystemMemoryAllocator* allocator = tt_device->get_system_memory_allocator()) {
        channel_plan = plan_host_mem_channels(
            *allocator, tt_device->get_arch(), num_host_mem_channels, tt_device->get_communication_device_id());
    }

    return std::unique_ptr<LocalChip>(
        new LocalChip(std::move(tt_device), channel_plan.num_channels, channel_plan.single_buffer));
}

LocalChip::LocalChip(
    std::unique_ptr<TTDevice> tt_device, uint32_t num_host_mem_channels, bool host_mem_in_single_buffer) :
    Chip(tt_device->get_chip_info(), tt_device->get_arch()),
    system_memory_allocator_(tt_device->get_system_memory_allocator()),
    host_mem_channels_(num_host_mem_channels),
    host_mem_in_single_buffer_(host_mem_in_single_buffer),
    tt_device_(std::move(tt_device)) {
    l1_address_params = tt_device_->get_architecture_implementation()->get_l1_address_params();
    tt_device_->set_power_state(TTDevice::PowerState::BUSY);
    wait_chip_to_be_ready();

    // start_device() and the memory barriers take these locks whatever the transport.
    initialize_default_chip_mutexes();
}

LocalChip::~LocalChip() {
    // Deconstruct the LocalChip in the right order.
    // TODO: Use intializers in constructor to avoid having to explicitly declare the order of destruction.
    tt_device_->set_power_state(TTDevice::PowerState::IDLE);
    cached_wc_tlb_window.reset();
    cached_uc_tlb_window.reset();
    unmap_host_mem_channels();
    tt_device_.reset();
}

void LocalChip::initialize_default_chip_mutexes() {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of
    // time here (during device init) since it's unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is
    // responsible for initializing the driver with this field set to cleanup after an aborted process.
    int device_id = tt_device_->get_communication_device_id();
    IODeviceType device_type = tt_device_->get_communication_device_type();

    // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for
    // ethernet broadcast
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        LockManager::initialize_mutex(MutexType::REMOTE_ARC_MSG, device_id, device_type);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic.
    LockManager::initialize_mutex(MutexType::MEM_BARRIER, device_id, device_type);

    // Initialize mutex guarding initialized chips.
    LockManager::initialize_mutex(MutexType::CHIP_IN_USE, device_id, device_type);
}

void LocalChip::initialize_membars(uint32_t dram_subchannel) {
    ZoneScopedC(tracy::Color::DarkGreen);
    set_membar_flag(
        get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED),
        MemBarFlag::RESET,
        l1_address_params.tensix_l1_barrier_base);
    set_membar_flag(
        get_soc_descriptor().get_cores(CoreType::ETH, CoordSystem::TRANSLATED),
        MemBarFlag::RESET,
        l1_address_params.eth_l1_barrier_base);

    std::vector<CoreCoord> dram_cores_vector = {};
    dram_cores_vector.reserve(get_soc_descriptor().get_num_dram_channels());
    for (std::uint32_t dram_idx = 0; dram_idx < get_soc_descriptor().get_num_dram_channels(); dram_idx++) {
        dram_cores_vector.push_back(
            get_soc_descriptor().get_dram_core_for_channel(dram_idx, dram_subchannel, CoordSystem::TRANSLATED));
    }
    set_membar_flag(dram_cores_vector, MemBarFlag::RESET, dram_address_params.DRAM_BARRIER_BASE);
}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

SystemMemoryAllocator* LocalChip::get_system_memory_allocator() { return system_memory_allocator_; }

const HostMemChannels* LocalChip::get_host_mem_channels() { return &host_mem_channels_; }

bool LocalChip::is_mmio_capable() const { return true; }

void LocalChip::start_device(uint32_t dram_membar_subchannel) {
    ZoneScopedC(tracy::Color::DarkGreen);
    // A transport that reports no host memory channels has no sysmem to pin, and the barriers
    // initialize_membars() resets live in core memory this path cannot reach yet. JTAG is in the
    // same position, so both leave the device started with neither.
    if (tt_device_->get_communication_device_type() == IODeviceType::JTAG || system_memory_allocator_ == nullptr) {
        return;
    }

    // TODO: acquire mutex should live in Chip class. Currently we don't have unique id for all chips.
    // The lock here should suffice since we have to open Local chip to have Remote chips initialized.
    chip_started_lock_.emplace(LockManager::acquire_mutex(
        MutexType::CHIP_IN_USE,
        tt_device_->get_communication_device_id(),
        tt_device_->get_communication_device_type()));

    // A failed start leaves the chip as if it was never started, so it can be closed or started again.
    try {
        map_host_mem_channels();
        initialize_membars(dram_membar_subchannel);
    } catch (...) {
        unmap_host_mem_channels();
        chip_started_lock_.reset();
        throw;
    }
}

void LocalChip::close_device() {
    ZoneScopedC(tracy::Color::DarkRed);
    // Investigating https://github.com/tenstorrent/tt-metal/issues/25377 found that closing device that was already put
    // in LONG_IDLE by tt-smi reset would hang
    if ((uint32_t)get_clock() != get_tt_device()->get_min_clock_freq()) {
        set_clock_state(DevicePowerState::LONG_IDLE);
        assert_risc_reset(RiscType::ALL);
    }
    // Outside the clock check: a chip already at minimum clock still holds the channels start_device mapped.
    unmap_host_mem_channels();
    chip_started_lock_.reset();
};

void LocalChip::map_host_mem_channels() {
    const size_t num_channels = host_mem_channels_.get_num_channels();
    if (num_channels == 0) {
        return;
    }
    const tt::ARCH arch = tt_device_->get_arch();
    const uint64_t noc_base = system_memory_allocator_->get_noc_base();
    const int device_id = tt_device_->get_communication_device_id();

    // Only the silicon manager knows which hugepage belongs to which channel, so on silicon the channel memory comes
    // from it in either layout.
    auto* silicon_sysmem_manager = dynamic_cast<SiliconSysmemManager*>(system_memory_allocator_);

    if (host_mem_in_single_buffer_) {
        const size_t span = get_host_mem_channels_span(arch, num_channels);
        // One buffer bound to the NOC places channel N at its expected NOC address, as long as the buffer itself
        // lands at noc_base.
        std::unique_ptr<SysmemBuffer> buffer = allocate_host_mem_buffer(
            [&] {
                return silicon_sysmem_manager != nullptr
                           ? silicon_sysmem_manager->allocate_channel_buffer(0, span)
                           : system_memory_allocator_->allocate_buffer(span, /*bind_to_noc=*/true);
            },
            noc_base,
            0,
            num_channels,
            device_id);
        auto* base = static_cast<uint8_t*>(buffer->get_va());
        for (size_t channel = 0; channel < num_channels; channel++) {
            host_mem_channels_.map_channel(
                channel, base + channel * HostMemChannels::CHANNEL_STRIDE, get_host_mem_channel_size(arch, channel));
        }
        host_mem_channel_buffers_.push_back(std::move(buffer));
        return;
    }

    // Each channel is its own buffer. Every channel a chip reports is used, so one that cannot be allocated or lands
    // elsewhere on the NOC fails the start rather than surfacing later as an unmapped or misplaced channel.
    UMD_ASSERT(
        silicon_sysmem_manager != nullptr,
        error::RuntimeError,
        "Host memory channels need a silicon sysmem manager to allocate one buffer per channel.");
    for (size_t channel = 0; channel < num_channels; channel++) {
        const size_t channel_size = get_host_mem_channel_size(arch, channel);
        // The truncated channel is the final one, so the channels before it all sit on the CHANNEL_STRIDE grid.
        std::unique_ptr<SysmemBuffer> buffer = allocate_host_mem_buffer(
            [&] {
                return silicon_sysmem_manager->allocate_channel_buffer(static_cast<uint32_t>(channel), channel_size);
            },
            noc_base + channel * HostMemChannels::CHANNEL_STRIDE,
            channel,
            1,
            device_id);
        host_mem_channels_.map_channel(channel, buffer->get_va(), channel_size);
        host_mem_channel_buffers_.push_back(std::move(buffer));
    }
}

void LocalChip::unmap_host_mem_channels() {
    host_mem_channels_.unmap_channels();
    host_mem_channel_buffers_.clear();
}

size_t LocalChip::get_num_host_channels() {
    // log_warning instead of throw because even though system_memory_allocator_ may not be set in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (system_memory_allocator_ == nullptr) {
        log_warning(
            LogUMD,
            "No system memory allocator for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    return host_mem_channels_.get_num_channels();
}

size_t LocalChip::get_host_channel_size(std::uint32_t channel) {
    // log_warning instead of throw because even though system_memory_allocator_ may not be set in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (system_memory_allocator_ == nullptr) {
        log_warning(
            LogUMD,
            "No system memory allocator for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    UMD_ASSERT(
        channel < get_num_host_channels(),
        error::RuntimeError,
        "Querying size for a host channel that does not exist.");
    const size_t channel_size = host_mem_channels_.get_channel_size(channel);
    UMD_ASSERT(
        channel_size, error::RuntimeError, "Host channel size can only be queried after the device has been started.");
    return channel_size;
}

void LocalChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    // log_warning instead of throw because even though system_memory_allocator_ may not be set in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (system_memory_allocator_ == nullptr) {
        log_warning(
            LogUMD,
            "No system memory allocator for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    host_mem_channels_.write_to_channel(channel, src, sysmem_dest, size);
}

void LocalChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    // log_warning instead of throw because even though system_memory_allocator_ may not be set in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (system_memory_allocator_ == nullptr) {
        log_warning(
            LogUMD,
            "No system memory allocator for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    host_mem_channels_.read_from_channel(channel, dest, sysmem_src, size);
}

void LocalChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size, IoOrdering ordering) {
    log_trace(
        LogUMD,
        "Chip::write_to_device to {} dev {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_dest,
        size);

    // The coordinate travels whole: TTDevice translates it exactly as this would have, and an
    // architecture that folds the coordinate into the address needs what the translation drops.
    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->write_to_device(src, core, l1_dest, size, get_selected_noc_id());
        return;
    }

    tt_xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());

    // Whatever the mode, the underlying MMIO stores are posted: the call returns once issued, not
    // once landed. This path reconfigures the window between transfers, so nothing else orders them.
    std::lock_guard<std::mutex> lock(wc_tlb_lock);
    write_block_reconfigure(
        *get_cached_wc_tlb_window(), src, translated_core, l1_dest, size, get_selected_noc_id(), ordering);
}

void LocalChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size, IoOrdering ordering) {
    log_trace(
        LogUMD,
        "Chip::read_from_device from {} device {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_src,
        size);

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->read_from_device(dest, core, l1_src, size, get_selected_noc_id());
        return;
    }

    tt_xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    std::lock_guard<std::mutex> lock(wc_tlb_lock);
    read_block_reconfigure(
        *get_cached_wc_tlb_window(), dest, translated_core, l1_src, size, get_selected_noc_id(), ordering);
}

void LocalChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    tt_device_->dma_write(src, addr, size, core, get_selected_noc_id());
}

void LocalChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    tt_device_->dma_read(dst, addr, size, core, get_selected_noc_id());
}

void LocalChip::dma_write_zero_copy(
    const SysmemBuffer& buffer, size_t offset, size_t size, CoreCoord core, uint64_t addr) {
    check_zero_copy_buffer(buffer, tt_device_->get_communication_device_id(), offset, size);
    tt_device_->dma_write_zero_copy(buffer.get_iova(offset), addr, size, core, get_selected_noc_id());
}

void LocalChip::dma_read_zero_copy(SysmemBuffer& buffer, size_t offset, size_t size, CoreCoord core, uint64_t addr) {
    UMD_ASSERT(
        buffer.get_device_access() != DeviceBufferAccess::READ_ONLY,
        error::RuntimeError,
        "Cannot DMA from the device into a device-read-only host mapping.");
    check_zero_copy_buffer(buffer, tt_device_->get_communication_device_id(), offset, size);
    tt_device_->dma_read_zero_copy(buffer.get_iova(offset), addr, size, core, get_selected_noc_id());
}

int LocalChip::export_dmabuf(CoreCoord core, uint64_t addr, size_t size, IoOrdering ordering) {
    return tt_device_->export_dmabuf(core, addr, size, ordering, get_selected_noc_id());
}

void LocalChip::dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    tt_device_->dma_write_to_core_range(src, addr, size, core_start, core_end, get_selected_noc_id());
}

void LocalChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Size must be a multiple of 4 bytes.");
    }

    if (reg_dest % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Register address must be 4-byte aligned.");
    }

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->write_to_device_reg(src, core, reg_dest, size, get_selected_noc_id());
        return;
    }

    std::lock_guard<std::mutex> lock(uc_tlb_lock);

    auto translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    write_register_reconfigure(
        *get_cached_uc_tlb_window(),
        src,
        translated_core,
        reg_dest,
        size,
        is_selected_noc1() ? NocId::NOC1 : NocId::NOC0,
        RegisterAccessWidth::BITS32);
}

void LocalChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Size must be a multiple of 4 bytes.");
    }

    if (reg_src % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Register address must be 4-byte aligned.");
    }

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->read_from_device_reg(dest, core, reg_src, size, get_selected_noc_id());
        return;
    }

    auto translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());

    std::lock_guard<std::mutex> lock(uc_tlb_lock);

    read_register_reconfigure(
        *get_cached_uc_tlb_window(),
        dest,
        translated_core,
        reg_src,
        size,
        is_selected_noc1() ? NocId::NOC1 : NocId::NOC0,
        RegisterAccessWidth::BITS32);
}

void LocalChip::wait_for_non_mmio_flush() {}

void LocalChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>&) {}

void LocalChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>&) {}

void LocalChip::set_membar_flag(
    const std::vector<CoreCoord>& cores, const uint32_t barrier_value, const uint32_t barrier_addr) {
    tt_driver_atomics::sfence();  // Ensure that writes before this do not get reordered
    std::unordered_set<CoreCoord> cores_synced = {};
    std::vector<uint32_t> barrier_val_vec = {barrier_value};
    for (const auto& core : cores) {
        write_to_device(core, barrier_val_vec.data(), barrier_addr, barrier_val_vec.size() * sizeof(uint32_t));
    }
    tt_driver_atomics::sfence();  // Ensure that all writes in the Host WC buffer are flushed
    const auto start = std::chrono::steady_clock::now();
    while (cores_synced.size() != cores.size()) {
        for (const auto& core : cores) {
            if (cores_synced.find(core) == cores_synced.end()) {
                uint32_t readback_val;
                read_from_device(core, &readback_val, barrier_addr, sizeof(std::uint32_t));
                if (readback_val == barrier_value) {
                    cores_synced.insert(core);
                } else if (utils::check_timeout(start, timeout::MEMBAR_SYNC_TIMEOUT)) {
                    // Report the value actually read. A core that answers but returns corrupted data
                    // (e.g. a DRAM channel that trained with a dead byte lane) never converges, and the
                    // readback is what identifies it.
                    UMD_THROW(
                        error::RuntimeError,
                        fmt::format(
                            "Memory barrier timed out after {} ms on device {} core {} at {:#x}: expected {:#x}, "
                            "read {:#x}. {} of {} core(s) synced.",
                            timeout::MEMBAR_SYNC_TIMEOUT.count(),
                            tt_device_->get_communication_device_id(),
                            core.str(),
                            barrier_addr,
                            barrier_value,
                            readback_val,
                            cores_synced.size(),
                            cores.size()));
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned.
    tt_driver_atomics::mfence();
}

void LocalChip::insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr) {
    // Ensure that this memory barrier is atomic across processes/threads.
    auto lock = LockManager::acquire_mutex(
        MutexType::MEM_BARRIER, tt_device_->get_communication_device_id(), tt_device_->get_communication_device_type());
    set_membar_flag(cores, MemBarFlag::SET, barrier_addr);
    set_membar_flag(cores, MemBarFlag::RESET, barrier_addr);
}

void LocalChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {
    const bool include_dram_in_l1_membar = get_soc_descriptor().arch == tt::ARCH::BLACKHOLE;
    if (!cores.empty()) {
        // Insert barrier on specific cores with L1.
        std::vector<CoreCoord> workers_to_sync = {};
        std::vector<CoreCoord> eth_to_sync = {};
        std::vector<CoreCoord> dram_to_sync = {};

        for (const auto& core : cores) {
            auto core_from_soc = get_soc_descriptor().get_coord_at(core.to_pair(), core.coord_system);
            if (core_from_soc.core_type == CoreType::TENSIX) {
                workers_to_sync.push_back(core);
            } else if (core_from_soc.core_type == CoreType::ETH) {
                eth_to_sync.push_back(core);
            } else if (include_dram_in_l1_membar && core_from_soc.core_type == CoreType::DRAM) {
                dram_to_sync.push_back(core);
            } else {
                UMD_THROW(error::RuntimeError, "Can only insert an L1 Memory barrier on Tensix or Ethernet cores.");
            }
        }
        insert_host_to_device_barrier(workers_to_sync, l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(eth_to_sync, l1_address_params.eth_l1_barrier_base);
        if (include_dram_in_l1_membar) {
            insert_host_to_device_barrier(dram_to_sync, dram_address_params.DRAM_BARRIER_BASE);
        }
    } else {
        // Insert barrier on all cores with L1.
        insert_host_to_device_barrier(
            get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED),
            l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(
            get_soc_descriptor().get_cores(CoreType::ETH, CoordSystem::TRANSLATED),
            l1_address_params.eth_l1_barrier_base);
        if (include_dram_in_l1_membar) {
            insert_host_to_device_barrier(
                get_soc_descriptor().get_cores(CoreType::DRAM, CoordSystem::TRANSLATED),
                dram_address_params.DRAM_BARRIER_BASE);
        }
    }
}

void LocalChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {
    if (!cores.empty()) {
        for (const auto& core : cores) {
            UMD_ASSERT(
                get_soc_descriptor().get_coord_at(core.to_pair(), core.coord_system).core_type == CoreType::DRAM,
                error::RuntimeError,
                "Can only insert a DRAM Memory barrier on DRAM cores.");
        }
        std::vector<CoreCoord> dram_cores_vector = std::vector<CoreCoord>(cores.begin(), cores.end());
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    } else {
        // Insert Barrier on all DRAM Cores.
        std::vector<CoreCoord> dram_cores_vector = {};
        dram_cores_vector.reserve(get_soc_descriptor().get_num_dram_channels());
        for (std::uint32_t dram_idx = 0; dram_idx < get_soc_descriptor().get_num_dram_channels(); dram_idx++) {
            dram_cores_vector.push_back(
                get_soc_descriptor().get_dram_core_for_channel(dram_idx, 0, CoordSystem::TRANSLATED));
        }
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    }
}

void LocalChip::dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel) {
    std::unordered_set<CoreCoord> dram_cores_to_sync = {};
    for (const auto& chan : channels) {
        dram_cores_to_sync.insert(
            get_soc_descriptor().get_dram_core_for_channel(chan, subchannel, CoordSystem::TRANSLATED));
    }
    dram_membar(dram_cores_to_sync);
}

void LocalChip::deassert_risc_resets() {
    ZoneScopedC(tracy::Color::DarkGreen);
    // A Wormhole firmware message: other architectures leave their RISCs in reset until a core is deasserted.
    if (get_soc_descriptor().arch == tt::ARCH::WORMHOLE_B0) {
        arc_msg(
            wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::DEASSERT_RISCV_RESET),
            true,
            {0, 0});
    }
}

int LocalChip::get_clock() { return tt_device_->get_clock(); }

int LocalChip::get_numa_node() {
    PcieInterface* pcie_interface = tt_device_->get_pcie_interface();
    return pcie_interface != nullptr ? pcie_interface->get_numa_node() : -1;
}

std::unique_ptr<IoWindow> LocalChip::create_cached_window(HostMemoryCaching caching) {
    // Every transfer reconfigures the window before using it, so the target it starts out with is never used.
    return tt_device_->create_io_window(
        TargetIoWindowConfig{},
        HostIoWindowConfig{caching, get_architecture_tlbs(tt_device_->get_arch()).cached_window_size});
}

IoWindow* LocalChip::get_cached_wc_tlb_window() {
    if (cached_wc_tlb_window == nullptr) {
        cached_wc_tlb_window = create_cached_window(HostMemoryCaching::WC);
    }

    return cached_wc_tlb_window.get();
}

IoWindow* LocalChip::get_cached_uc_tlb_window() {
    if (cached_uc_tlb_window == nullptr) {
        cached_uc_tlb_window = create_cached_window(HostMemoryCaching::UC);
    }

    return cached_uc_tlb_window.get();
}
}  // namespace tt::umd
