/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/sysmem_manager.hpp"

#include <sys/mman.h>  // for mmap, munmap
#include <sys/stat.h>  // for fstat

#include <filesystem>
#include <fstream>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "cpuset_lib.hpp"
#include "hugepage.hpp"

namespace tt::umd {

SimulationSysmemManager::SimulationSysmemManager(TLBManager *tlb_manager, uint32_t num_host_mem_channels) :
    SysmemManager() {
    init_hugepages(num_host_mem_channels);
}

SimulationSysmemManager::SimulationSysmemManager() { 
    
}

bool SimulationSysmemManager::pin_or_map_sysmem_to_device() {
    // if (tt_device_->get_pci_device()->is_iommu_enabled()) {
    //     return pin_or_map_iommu();
    // } else {
    return pin_or_map_hugepages();
    // }
}

SimulationSysmemManager::~SimulationSysmemManager() { unpin_or_unmap_sysmem(); }

void SimulationSysmemManager::unpin_or_unmap_sysmem() {
}

void SimulationSysmemManager::write_to_sysmem(uint16_t channel, const void *src, uint64_t sysmem_dest, uint32_t size) {
    HugepageMapping hugepage_map = get_hugepage_mapping(channel);
    TT_ASSERT(
        hugepage_map.mapping,
        "write_buffer: Hugepages are not allocated for pci device num: {} ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        tt_device_->get_pci_device()->get_device_num(),
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
        "read_buffer: Hugepages are not allocated for pci device num: {} ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        tt_device_->get_pci_device()->get_device_num(),
        channel);

    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_src % hugepage_map.mapping_size);

    log_debug(
        LogUMD,
        "Cluster::read_buffer (pci device num: {}, ch: {}) from {:p}",
        tt_device_->get_pci_device()->get_device_num(),
        channel,
        user_scratchspace);

    memcpy(dest, user_scratchspace, size);
}

bool SimulationSysmemManager::init_hugepages(uint32_t num_host_mem_channels) {

    uint64_t total_size = 0;

    

    if (num_host_mem_channels == 0) {
        // No hugepage channels requested, so just skip initialization.
        return true;
    }
    // TODO: get rid of this when the following Metal CI issue is resolved.
    // https://github.com/tenstorrent/tt-metal/issues/15675
    // The notion that we should clamp the number of host mem channels to
    // what we have available and emit a warning is wrong, since the
    // application might try to use the channels it asked for.  We should
    // just fail early since the error message will be actionable instead of
    // a segfault or memory corruption.
    uint16_t pcie_device_id = tt_device_->get_pci_device()->get_pci_device_id();
    uint32_t pcie_revision = tt_device_->get_pci_device()->get_pci_revision();
    num_host_mem_channels = get_available_num_host_mem_channels(num_host_mem_channels, pcie_device_id, pcie_revision);

    log_debug(
        LogUMD,
        "Using {} Hugepages/NumHostMemChannels for PCIDevice {}",
        num_host_mem_channels,
        tt_device_->get_pci_device()->get_device_num());

    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;
    auto physical_device_id = tt_device_->get_pci_device()->get_device_num();

    std::string hugepage_dir = find_hugepage_dir(hugepage_size);
    if (hugepage_dir.empty()) {
        log_warning(
            LogUMD, "SysmemManager::init_hugepage: no huge page mount found for hugepage_size: {}.", hugepage_size);
        return false;
    }

    bool success = true;

    hugepage_mapping_per_channel.resize(num_host_mem_channels);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < num_host_mem_channels; ch++) {
        int hugepage_fd = open_hugepage_file(hugepage_dir, physical_device_id, ch);
        if (hugepage_fd == -1) {
            // Probably a permissions problem.
            log_warning(
                LogUMD,
                "SysmemManager::init_hugepage: physical_device_id: {} ch: {} creating hugepage mapping file failed.",
                physical_device_id,
                ch);
            success = false;
            continue;
        }

        // Verify opened file size.
        struct stat hugepage_st;
        if (fstat(hugepage_fd, &hugepage_st) == -1) {
            log_warning(LogUMD, "Error reading hugepage file size after opening.");
        }

        std::byte *mapping = static_cast<std::byte *>(
            mmap(nullptr, hugepage_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, hugepage_fd, 0));

        close(hugepage_fd);

        if (mapping == MAP_FAILED) {
            log_warning(
                LogUMD,
                "UMD: Mapping a hugepage failed. (device: {}, {}/{} errno: {}).",
                physical_device_id,
                ch,
                num_host_mem_channels,
                strerror(errno));
            if (hugepage_st.st_size == 0) {
                log_warning(
                    LogUMD,
                    "Opened hugepage file has zero size, mapping might've failed due to that. Verify that enough "
                    "hugepages are provided.");
            }
            print_file_contents("/proc/cmdline");
            print_file_contents(
                "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages");  // Hardcoded for 1GB hugepage.
            success = false;
            continue;
        }

        // Beter performance if hugepage just allocated (populate flag to prevent lazy alloc) is migrated to same
        // numanode as TT device.
        if (!tt::cpuset::cpuset_allocator::bind_area_to_memory_nodeset(physical_device_id, mapping, hugepage_size)) {
            log_warning(
                LogUMD,
                "---- ttSiliconDevice::init_hugepage: bind_area_to_memory_nodeset() failed (physical_device_id: {} ch: "
                "{}). "
                "Hugepage allocation is not on NumaNode matching TT Device. Side-Effect is decreased Device->Host perf "
                "(Issue #893).",
                physical_device_id,
                ch);
        }

        hugepage_mapping_per_channel[ch] = {mapping, hugepage_size, 0};
    }

    return success;
}

bool SimulationSysmemManager::pin_or_map_hugepages() {
    return true;
}

bool SimulationSysmemManager::init_iommu(uint32_t num_fake_mem_channels) {
    return true;
}

bool SimulationSysmemManager::pin_or_map_iommu() {
    return true;
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(size_t sysmem_buffer_size, const bool map_to_noc) {
    return nullptr;
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc) {
    return nullptr;
}

}  // namespace tt::umd
