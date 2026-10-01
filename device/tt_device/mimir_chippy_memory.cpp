// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "mimir_chippy_memory.hpp"

#include <fmt/format.h>

#include <cstring>
#include <vector>

#include "mimir.h"  // chippy
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr size_t kMinWordSizeBytes = 4;
constexpr std::size_t kCcesPerMimir = 2;

const uint64_t kMimirConfigLocalBase = chippy::grendel::kMimirSmcLocalAddr;
const uint64_t kMimirGddrDramLocalBase = chippy::grendel::kMimirGddrDramLocalAddr;
const uint64_t kMimirCceSramLocalBase = chippy::grendel::kMimirCce0SramLocalAddr;
const uint64_t kMimirCceSramStride = chippy::grendel::kMimirCceSramSize;

}  // namespace

bool MimirChippyMemoryMap::MemoryRegion::contains(uint64_t address, std::size_t size) const {
    if (address < flat_base) {
        return false;
    }
    const uint64_t region_offset = address - flat_base;
    return region_offset < memory->size_bytes() && size <= memory->size_bytes() - region_offset;
}

std::size_t MimirChippyMemoryMap::MemoryRegion::offset(uint64_t address) const {
    return static_cast<std::size_t>(address - flat_base);
}

uint64_t MimirChippyMemoryMap::MemoryRegion::local_address(uint64_t address) const {
    return local_base + offset(address);
}

void MimirChippyMemoryMap::MemoryRegion::read(uint64_t address, void* dst, std::size_t size) const {
    const std::size_t byte_offset = offset(address);
    if (address % kMinWordSizeBytes == 0 && size % kMinWordSizeBytes == 0) {
        const auto bytes = memory->bulk_read_bytes(byte_offset, size);
        std::memcpy(dst, bytes.data(), bytes.size());
        return;
    }
    transport->read(size, kMinWordSizeBytes, local_address(address), dst);
}

void MimirChippyMemoryMap::MemoryRegion::write(uint64_t address, const void* src, std::size_t size) {
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

MimirChippyMemoryMap::MimirChippyMemoryMap(
    const SocDescriptor& soc_descriptor,
    std::vector<std::shared_ptr<Transport>> chiplet_transports,
    std::string device_name) :
    device_name_(std::move(device_name)) {
    const auto windows = mimir_local_address_windows(soc_descriptor);
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    UMD_ASSERT(
        chiplet_transports.size() == mimir_count,
        error::RuntimeError,
        fmt::format(
            "{} expected {} chiplet transport(s), got {}.",
            device_name_,
            mimir_count,
            chiplet_transports.size()));

    for (std::size_t mimir_index = 0; mimir_index < mimir_count; ++mimir_index) {
        const uint64_t flat_config_base = windows.config_base + mimir_index * windows.config_stride;
        memory_regions_.push_back(make_region(
            flat_config_base, kMimirConfigLocalBase, windows.config_stride, chiplet_transports[mimir_index]));
    }

    const uint32_t locations_per_channel = static_cast<uint32_t>(soc_descriptor.get_grid_size(CoreType::DRAM).y);
    UMD_ASSERT(
        locations_per_channel == kCcesPerMimir,
        error::RuntimeError,
        fmt::format(
            "Each Mimir DRAM channel must expose {} locations, found {}.", kCcesPerMimir, locations_per_channel));
    for (uint32_t channel = 0; channel < soc_descriptor.get_num_dram_channels(); ++channel) {
        const auto& chiplet_transport = chiplet_transports.at(channel);
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
}

MimirChippyMemoryMap::MemoryRegion MimirChippyMemoryMap::make_region(
    uint64_t flat_base, uint64_t local_base, std::size_t size, const std::shared_ptr<Transport>& transport) {
    return {
        .flat_base = flat_base,
        .local_base = local_base,
        .transport = transport,
        .memory = std::make_unique<ChippyMemory>(
            transport.get(), local_base, local_base, use_global_addressing_, size)};
}

MimirChippyMemoryMap::MemoryRegion* MimirChippyMemoryMap::find_memory(uint64_t address, std::size_t size) const {
    for (auto& region : memory_regions_) {
        if (region.contains(address, size)) {
            return &region;
        }
    }
    return nullptr;
}

void MimirChippyMemoryMap::read(uint64_t flat_address, void* dst, std::size_t size) const {
    if (auto* region = find_memory(flat_address, size)) {
        region->read(flat_address, dst, size);
        return;
    }
    UMD_THROW(
        error::RuntimeError,
        fmt::format(
            "{} read at flat address 0x{:x} ({} bytes) is outside exposed Mimir memory.",
            device_name_,
            flat_address,
            size));
}

void MimirChippyMemoryMap::write(uint64_t flat_address, const void* src, std::size_t size) {
    if (auto* region = find_memory(flat_address, size)) {
        region->write(flat_address, src, size);
        return;
    }
    UMD_THROW(
        error::RuntimeError,
        fmt::format(
            "{} write at flat address 0x{:x} ({} bytes) is outside exposed Mimir memory.",
            device_name_,
            flat_address,
            size));
}

}  // namespace tt::umd
