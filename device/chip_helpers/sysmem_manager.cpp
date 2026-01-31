// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/sysmem_manager.hpp"

#include "assert.hpp"
#include "cpuset_lib.hpp"
#include "hugepage.hpp"

#include <sys/mman.h>  // for mmap, munmap
#include <sys/stat.h>  // for fstat
#include <tt-logger/tt-logger.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace tt::umd {

void SysmemManager::write_to_sysmem(uint16_t channel, const void *src, uint64_t sysmem_dest, uint32_t size) {
    HugepageMapping hugepage_map = get_hugepage_mapping(channel);
    TT_ASSERT(
        hugepage_map.mapping,
        "write_buffer: Hugepages are not allocated ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        channel);

    TT_ASSERT(
        size <= hugepage_map.mapping_size,
        "write_buffer data has larger size {} than destination buffer {}",
        size,
        hugepage_map.mapping_size);
    log_debug(
        LogUMD,
        "Using hugepage mapping at address {:p} offset {} chan {} size {}",
        hugepage_map.mapping,
        (sysmem_dest % hugepage_map.mapping_size),
        channel,
        size);
    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_dest % hugepage_map.mapping_size);

    memcpy(user_scratchspace, src, size);
}

void SysmemManager::read_from_sysmem(uint16_t channel, void *dest, uint64_t sysmem_src, uint32_t size) {
    HugepageMapping hugepage_map = get_hugepage_mapping(channel);
    TT_ASSERT(
        hugepage_map.mapping,
        "read_buffer: Hugepages are not allocated ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        channel);

    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_src % hugepage_map.mapping_size);

    if (tt_device_) {
        log_debug(
            LogUMD,
            "Cluster::read_buffer (comm. device ID: {}, ch: {}) from {:p}",
            tt_device_->get_communication_device_id(),
            channel,
            user_scratchspace);
    }
    memcpy(dest, user_scratchspace, size);
}

size_t SysmemManager::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

HugepageMapping SysmemManager::get_hugepage_mapping(size_t channel) const {
    if (hugepage_mapping_per_channel.size() <= channel) {
        return {nullptr, 0, 0};
    } else {
        return hugepage_mapping_per_channel[channel];
    }
}

}  // namespace tt::umd
