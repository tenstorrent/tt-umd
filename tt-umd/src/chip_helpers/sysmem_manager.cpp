// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

#include <fmt/format.h>

#include <cstring>
#include <tt-logger/tt-logger.hpp>

#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/utils/error.hpp"
#include <tt-umd/chip_helpers/host_cache.hpp>

namespace tt::umd {

namespace {

// A platform that does not snoop inbound DMA and cannot issue cache-maintenance instructions has no
// correct way to share sysmem with the device. Nothing here can fix that, so say so once and loudly
// rather than let it look like it worked.
void warn_once_if_maintenance_unavailable() {
    static const bool reported = [] {
        if (host_cache::maintenance_required_but_unavailable()) {
            log_error(
                LogUMD,
                "Host cache maintenance is required on this platform but unavailable: the interconnect "
                "does not report snooping this device's inbound DMA, and no usable cache-maintenance "
                "instruction was found. Sysmem shared with the device may return stale data in either "
                "direction.");
        }
        return true;
    }();
    (void)reported;
}

}  // namespace


void SysmemManager::write_to_sysmem(uint16_t channel, const void *src, uint64_t sysmem_dest, uint32_t size) {
    warn_once_if_maintenance_unavailable();
    ZoneScopedC(tracy::Color::Yellow);
    HugepageMapping hugepage_map = get_hugepage_mapping(channel);
    UMD_ASSERT(
        hugepage_map.mapping,
        error::RuntimeError,
        fmt::format(
            "write_buffer: Hugepages are not allocated ch: {}."
            " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
            channel));
    UMD_ASSERT(
        hugepage_map.mapping_size != 0,
        error::RuntimeError,
        fmt::format("Hugepage mapping size is 0 for channel {}.", channel));

    UMD_ASSERT(
        size <= hugepage_map.mapping_size,
        error::RuntimeError,
        fmt::format(
            "write_buffer data has larger size {} than destination buffer {}", size, hugepage_map.mapping_size));
    log_debug(
        LogUMD,
        "Using hugepage mapping at address {:p} offset {} chan {} size {}",
        hugepage_map.mapping,
        (sysmem_dest % hugepage_map.mapping_size),
        channel,
        size);
    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_dest % hugepage_map.mapping_size);

    memcpy(user_scratchspace, src, size);

    // The device DMA-reads this back out of DRAM, so the host's stores have to be written back on a
    // platform that does not snoop. No-op where it does.
    host_cache::clean_range(user_scratchspace, size);
}

void SysmemManager::read_from_sysmem(uint16_t channel, void *dest, uint64_t sysmem_src, uint32_t size) {
    warn_once_if_maintenance_unavailable();
    ZoneScopedC(tracy::Color::Yellow);
    HugepageMapping hugepage_map = get_hugepage_mapping(channel);
    UMD_ASSERT(
        hugepage_map.mapping,
        error::RuntimeError,
        fmt::format(
            "read_buffer: Hugepages are not allocated ch: {}."
            " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
            channel));
    UMD_ASSERT(
        hugepage_map.mapping_size != 0,
        error::RuntimeError,
        fmt::format("Hugepage mapping size is 0 for channel {}.", channel));

    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_src % hugepage_map.mapping_size);

    if (tt_device_) {
        log_debug(
            LogUMD,
            "SysmemManager: read_from_sysmem (comm. device ID: {}, ch: {}) from {:p}",
            tt_device_->get_communication_device_id(),
            channel,
            user_scratchspace);
    }
    // The device DMA-wrote this, so drop any cached lines first and let the copy refetch from DRAM.
    host_cache::invalidate_range(user_scratchspace, size);
    memcpy(dest, user_scratchspace, size);
}

std::unique_ptr<SysmemBuffer> SysmemManager::allocate_buffer(size_t size, bool bind_to_noc) {
    return allocate_sysmem_buffer(size, bind_to_noc);
}

std::unique_ptr<SysmemBuffer> SysmemManager::map_user_buffer(
    void *user_ptr, size_t size, bool bind_to_noc, DeviceBufferAccess device_access) {
    return map_sysmem_buffer(user_ptr, size, bind_to_noc, device_access);
}

uint64_t SysmemManager::get_pcie_base_for_arch(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return 0x800000000;
        case tt::ARCH::BLACKHOLE:
            return 4ULL << 58;
        default:
            return 0;
    }
}

size_t SysmemManager::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

bool SysmemManager::is_read_only_page_pinning_supported() const {
    // Managers without a PCI device behind them (simulation) cannot pin host pages at all.
    return pci_device_ != nullptr && pci_device_->is_read_only_page_pinning_supported();
}

HugepageMapping SysmemManager::get_hugepage_mapping(size_t channel) const {
    if (hugepage_mapping_per_channel.size() <= channel) {
        return {nullptr, 0, 0};
    } else {
        return hugepage_mapping_per_channel[channel];
    }
}

}  // namespace tt::umd
