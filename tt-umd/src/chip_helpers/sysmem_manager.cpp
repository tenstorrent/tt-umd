// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "hugepage.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Resolves a channel-relative address to host memory, checking that [sysmem_addr, sysmem_addr + size) lies in
// the channel. Addresses wrap at the hugepage size, as the device sees each channel as one hugepage region.
uint8_t* channel_host_address(
    const std::vector<HugepageMapping>& channels, uint16_t channel, uint64_t sysmem_addr, uint32_t size) {
    UMD_ASSERT(
        channel < channels.size() && channels[channel].mapping != nullptr,
        error::RuntimeError,
        "Host memory channel " + std::to_string(channel) + " is not set up.");
    const HugepageMapping& mapping = channels[channel];
    const uint64_t offset = sysmem_addr % HUGEPAGE_REGION_SIZE;
    UMD_ASSERT(
        offset <= mapping.mapping_size && size <= mapping.mapping_size - offset,
        error::RuntimeError,
        "Access of " + std::to_string(size) + " bytes at offset " + std::to_string(offset) +
            " runs past the end of host memory channel " + std::to_string(channel) + ".");
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
    std::memcpy(channel_host_address(hugepage_mapping_per_channel, channel, sysmem_dest, size), src, size);
}

void SysmemManager::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    std::memcpy(dest, channel_host_address(hugepage_mapping_per_channel, channel, sysmem_src, size), size);
}

size_t SysmemManager::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

HugepageMapping SysmemManager::get_hugepage_mapping(size_t channel) const {
    if (channel >= hugepage_mapping_per_channel.size()) {
        return {};
    }
    return hugepage_mapping_per_channel[channel];
}

bool SysmemManager::is_read_only_page_pinning_supported() const {
    return pci_device_ != nullptr && pci_device_->is_read_only_page_pinning_supported();
}

uint64_t SysmemManager::get_pcie_base_for_arch(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return 0x8'0000'0000ULL;
        case tt::ARCH::BLACKHOLE:
            // Selects the PCIe tile's fourth outbound ATU window.
            return 1ULL << 60;
        default:
            UMD_THROW(error::RuntimeError, "Architecture " + arch_to_str(arch) + " has no host memory window.");
    }
}

}  // namespace tt::umd
