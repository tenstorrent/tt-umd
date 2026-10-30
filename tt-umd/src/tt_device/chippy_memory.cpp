// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "chippy_memory.hpp"

#include <fmt/format.h>

#include <cstring>
#include <vector>

#include "mimir.h"  // chippy
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/coordinates/grendel_noc_address_resolver.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr size_t kMinWordSizeBytes = 4;
constexpr std::size_t kCcesPerMimir = 2;

const uint64_t kMimirConfigLocalBase = chippy::grendel::kMimirSmcLocalAddr;
const uint64_t kMimirGddrDramLocalBase = chippy::grendel::kMimirGddrDramLocalAddr;
const uint64_t kMimirCceSramLocalBase = chippy::grendel::kMimirCce0SramLocalAddr;
const uint64_t kMimirCceSramStride = chippy::grendel::kMimirCceSramSize;

}  // namespace

bool ChippyMemoryMap::MemoryRegion::contains(uint64_t address, std::size_t size) const {
    if (address < flat_base) {
        return false;
    }
    const uint64_t region_offset = address - flat_base;
    return region_offset < memory->size_bytes() && size <= memory->size_bytes() - region_offset;
}

std::size_t ChippyMemoryMap::MemoryRegion::offset(uint64_t address) const {
    return static_cast<std::size_t>(address - flat_base);
}

uint64_t ChippyMemoryMap::MemoryRegion::local_address(uint64_t address) const {
    return local_base + offset(address);
}

void ChippyMemoryMap::MemoryRegion::read(uint64_t address, void* dst, std::size_t size) const {
    const std::size_t byte_offset = offset(address);
    if (address % kMinWordSizeBytes == 0 && size % kMinWordSizeBytes == 0) {
        const auto bytes = memory->bulk_read_bytes(byte_offset, size);
        std::memcpy(dst, bytes.data(), bytes.size());
        return;
    }
    transport->read(size, kMinWordSizeBytes, local_address(address), dst);
}

void ChippyMemoryMap::MemoryRegion::write(uint64_t address, const void* src, std::size_t size) {
    const std::size_t byte_offset = offset(address);
    if (address % kMinWordSizeBytes == 0 && size % kMinWordSizeBytes == 0) {
        std::vector<uint8_t> bytes(size);
        std::memcpy(bytes.data(), src, size);
        memory->bulk_write_bytes(byte_offset, bytes);
        return;
    }
    // chippy's write() takes a non-const void* even though it only reads the buffer.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    transport->write(size, kMinWordSizeBytes, local_address(address), const_cast<void*>(src));
}

void ChippyMemoryMap::add_mimir_regions(
    const SocDescriptor& soc_descriptor,
    const std::vector<std::shared_ptr<Transport>>& chiplet_transports,
    std::size_t& transport_index) {
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    if (mimir_count == 0) {
        return;
    }
    const auto windows = mimir_local_address_windows(soc_descriptor);
    UMD_ASSERT(
        transport_index + mimir_count <= chiplet_transports.size(),
        error::RuntimeError,
        fmt::format(
            "{} needs {} Mimir transport(s) starting at {}, and {} were provided.",
            device_name_,
            mimir_count,
            transport_index,
            chiplet_transports.size()));

    for (std::size_t mimir_index = 0; mimir_index < mimir_count; ++mimir_index) {
        const auto& transport = chiplet_transports[transport_index + mimir_index];
        const uint64_t flat_config_base = windows.config_base + mimir_index * windows.config_stride;
        memory_regions_.push_back(
            make_region(flat_config_base, kMimirConfigLocalBase, windows.config_stride, transport));
    }

    const uint32_t locations_per_channel = static_cast<uint32_t>(soc_descriptor.get_grid_size(CoreType::DRAM).y);
    UMD_ASSERT(
        locations_per_channel == kCcesPerMimir,
        error::RuntimeError,
        fmt::format(
            "Each Mimir DRAM channel must expose {} locations, found {}.", kCcesPerMimir, locations_per_channel));
    for (uint32_t channel = 0; channel < soc_descriptor.get_num_dram_channels(); ++channel) {
        const auto& chiplet_transport = chiplet_transports[transport_index + channel];
        memory_regions_.push_back(make_region(
            windows.dram_base + static_cast<uint64_t>(channel) * windows.dram_stride,
            kMimirGddrDramLocalBase,
            windows.dram_stride,
            chiplet_transport));
        for (uint32_t location = 0; location < locations_per_channel; ++location) {
            const uint32_t l1_index = channel * locations_per_channel + location;
            memory_regions_.push_back(make_region(
                windows.dram_l1_base + static_cast<uint64_t>(l1_index) * windows.dram_l1_stride,
                kMimirCceSramLocalBase + location * kMimirCceSramStride,
                windows.dram_l1_size,
                chiplet_transport));
        }
    }
    transport_index += mimir_count;
}

void ChippyMemoryMap::add_keraunos_regions(
    const SocDescriptor& soc_descriptor,
    const std::vector<std::shared_ptr<Transport>>& chiplet_transports,
    std::size_t& transport_index) {
    const KeraunosChiplet& keraunos = soc_descriptor.get_arch_descriptor().get_keraunos();
    if (keraunos.chiplet.empty()) {
        return;
    }
    UMD_ASSERT(
        transport_index < chiplet_transports.size(),
        error::RuntimeError,
        fmt::format(
            "{} needs a Keraunos transport at {}, and {} were provided.",
            device_name_,
            transport_index,
            chiplet_transports.size()));
    // Flat and chiplet-local addresses are the same on this path. Control is the HSIO tile base;
    // SRAM is the descriptor window. They do not overlap, and both stay on this chiplet's socket.
    const auto& keraunos_transport = chiplet_transports[transport_index];
    ++transport_index;
    const uint64_t sram_size = soc_descriptor.get_arch_descriptor().get_eth_l1_size();
    UMD_ASSERT(
        keraunos.local_window.has_value() && keraunos.tile_stride.has_value() && sram_size > 0,
        error::RuntimeError,
        "A Keraunos chiplet needs local_window, tile_stride, and eth_l1_size.");
    for (const KeraunosCore& core : keraunos.cores) {
        const uint64_t sram = *keraunos.local_window + static_cast<uint64_t>(core.hsio) * *keraunos.tile_stride +
                              static_cast<uint64_t>(core.cce) * sram_size;
        memory_regions_.push_back(make_region(sram, sram, static_cast<std::size_t>(sram_size), keraunos_transport));
        const uint64_t control = grendel::KERAUNOS_HSIO0_LOCAL_BASE +
                                 static_cast<uint64_t>(core.hsio) * *keraunos.tile_stride +
                                 static_cast<uint64_t>(core.cce) * grendel::CCE_WITHIN_TILE_STRIDE;
        memory_regions_.push_back(make_region(
            control, control, static_cast<std::size_t>(grendel::CCE_WITHIN_TILE_STRIDE), keraunos_transport));
    }
}

ChippyMemoryMap::ChippyMemoryMap(
    const SocDescriptor& soc_descriptor,
    std::vector<std::shared_ptr<Transport>> chiplet_transports,
    std::string device_name) :
    device_name_(std::move(device_name)) {
    // Devices open sockets in this order. A later block takes the next transport.
    std::size_t transport_index = 0;
    add_mimir_regions(soc_descriptor, chiplet_transports, transport_index);
    add_keraunos_regions(soc_descriptor, chiplet_transports, transport_index);
    UMD_ASSERT(
        transport_index == chiplet_transports.size(),
        error::RuntimeError,
        fmt::format(
            "{} expected {} chiplet transport(s), got {}.",
            device_name_,
            transport_index,
            chiplet_transports.size()));
}

ChippyMemoryMap::MemoryRegion ChippyMemoryMap::make_region(
    uint64_t flat_base, uint64_t local_base, std::size_t size, const std::shared_ptr<Transport>& transport) {
    return {
        .flat_base = flat_base,
        .local_base = local_base,
        .transport = transport,
        .memory =
            std::make_unique<ChippyMemory>(transport.get(), local_base, local_base, use_global_addressing_, size)};
}

ChippyMemoryMap::MemoryRegion* ChippyMemoryMap::find_memory(uint64_t address, std::size_t size) const {
    for (auto& region : memory_regions_) {
        if (region.contains(address, size)) {
            return &region;
        }
    }
    return nullptr;
}

void ChippyMemoryMap::read(uint64_t flat_address, void* dst, std::size_t size) const {
    if (auto* region = find_memory(flat_address, size)) {
        region->read(flat_address, dst, size);
        return;
    }
    UMD_THROW(
        error::RuntimeError,
        fmt::format(
            "{} read at flat address 0x{:x} ({} bytes) is outside exposed chiplet memory.",
            device_name_,
            flat_address,
            size));
}

void ChippyMemoryMap::write(uint64_t flat_address, const void* src, std::size_t size) {
    if (auto* region = find_memory(flat_address, size)) {
        region->write(flat_address, src, size);
        return;
    }
    UMD_THROW(
        error::RuntimeError,
        fmt::format(
            "{} write at flat address 0x{:x} ({} bytes) is outside exposed chiplet memory.",
            device_name_,
            flat_address,
            size));
}

}  // namespace tt::umd
