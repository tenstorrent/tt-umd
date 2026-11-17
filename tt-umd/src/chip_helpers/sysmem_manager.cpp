// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

#include <fmt/format.h>

#include <cstring>

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/kmd_versions.hpp"

namespace tt::umd {

namespace {

// Returns the host address of [offset, offset + size) inside a channel, after checking it lies within the channel.
uint8_t* channel_address(
    const std::vector<HugepageMapping>& channels, uint16_t channel, uint64_t offset, uint32_t size) {
    UMD_ASSERT(
        channel < channels.size() && channels[channel].mapping != nullptr,
        error::RuntimeError,
        fmt::format("Host memory channel {} is not mapped.", channel));
    const HugepageMapping& mapping = channels[channel];
    UMD_ASSERT(
        offset <= mapping.mapping_size && size <= mapping.mapping_size - offset,
        error::RuntimeError,
        fmt::format(
            "Range [{:#x}, {:#x}) is outside host memory channel {} of {:#x} bytes.",
            offset,
            offset + size,
            channel,
            mapping.mapping_size));
    return static_cast<uint8_t*>(mapping.mapping) + offset;
}

}  // namespace

std::unique_ptr<SysmemBuffer> SysmemManager::allocate_buffer(size_t size, bool bind_to_noc) {
    return allocate_sysmem_buffer(size, bind_to_noc);
}

std::unique_ptr<SysmemBuffer> SysmemManager::map_user_buffer(
    void* user_ptr, size_t size, bool bind_to_noc, DeviceBufferAccess device_access) {
    return map_sysmem_buffer(user_ptr, size, bind_to_noc, device_access);
}

void SysmemManager::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    std::memcpy(channel_address(hugepage_mapping_per_channel, channel, sysmem_dest, size), src, size);
}

void SysmemManager::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    std::memcpy(dest, channel_address(hugepage_mapping_per_channel, channel, sysmem_src, size), size);
}

size_t SysmemManager::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

HugepageMapping SysmemManager::get_hugepage_mapping(size_t channel) const {
    // Callers treat an empty mapping as "no such channel", so a missing channel is not an error here.
    if (channel >= hugepage_mapping_per_channel.size()) {
        return {};
    }
    return hugepage_mapping_per_channel[channel];
}

bool SysmemManager::is_read_only_page_pinning_supported() const {
    // Only an IOMMU mapping can carry page permissions, and only newer KMDs pass the read-only flag through.
    return pci_device_ != nullptr && pci_device_->is_iommu_enabled() &&
           PCIDevice::read_kmd_version() >= KMD_READ_ONLY_PAGE_PINNING;
}

uint64_t SysmemManager::get_pcie_base_for_arch(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            // Start of the PCIe tile's NOC-to-host space, which always goes through the outbound iATU.
            return 0x800000000ULL;
        case tt::ARCH::BLACKHOLE:
            // The KMD's NOC window into host memory (window 4 of the PCIe tile's 2^58-byte windows).
            return 1ULL << 60;
        default:
            return 0;
    }
}

}  // namespace tt::umd
