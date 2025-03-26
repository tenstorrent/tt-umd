/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/sysmem_manager.h"

#include "logger.hpp"

namespace tt::umd {

SysmemManager::SysmemManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void SysmemManager::write_to_sysmem(const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel) {
    hugepage_mapping hugepage_map = tt_device_->get_pci_device()->get_hugepage_mapping(channel);
    log_assert(
        hugepage_map.mapping,
        "write_buffer: Hugepages are not allocated for pci device num: {} ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        tt_device_->get_pci_device()->get_device_num(),
        channel);

    log_assert(
        size <= hugepage_map.mapping_size,
        "write_buffer data has larger size {} than destination buffer {}",
        size,
        hugepage_map.mapping_size);
    log_debug(
        LogSiliconDriver,
        "Using hugepage mapping at address {} offset {} chan {} size {}",
        hugepage_map.mapping,
        (addr % hugepage_map.mapping_size),
        channel,
        size);
    void* user_scratchspace = static_cast<char*>(hugepage_map.mapping) + (addr % hugepage_map.mapping_size);

    memcpy(user_scratchspace, mem_ptr, size);
}

void SysmemManager::read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size) {
    hugepage_mapping hugepage_map = tt_device_->get_pci_device()->get_hugepage_mapping(channel);
    log_assert(
        hugepage_map.mapping,
        "read_buffer: Hugepages are not allocated for pci device num: {} ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        tt_device_->get_pci_device()->get_device_num(),
        channel);

    void* user_scratchspace = static_cast<char*>(hugepage_map.mapping) + (addr % hugepage_map.mapping_size);

    log_debug(
        LogSiliconDriver,
        "Cluster::read_buffer (pci device num: {}, ch: {}) from 0x{:x}",
        tt_device_->get_pci_device()->get_device_num(),
        channel,
        user_scratchspace);

    memcpy(mem_ptr, user_scratchspace, size);
}

}  // namespace tt::umd
