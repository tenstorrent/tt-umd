/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/sysmem_manager.h"

#include <sys/mman.h>  // for mmap, munmap
#include <sys/stat.h>  // for fstat

#include <fstream>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "cpuset_lib.hpp"
#include "umd/device/hugepage.h"

namespace tt::umd {

SysmemManager::SysmemManager(TLBManager *tlb_manager, uint32_t num_host_mem_channels) :
    tlb_manager_(tlb_manager), tt_device_(tlb_manager_->get_tt_device()) {
    TT_ASSERT(
        num_host_mem_channels <= 4,
        "Only 4 host memory channels are supported per device, but {} requested.",
        num_host_mem_channels);
    if (tt_device_->get_pci_device()->is_iommu_enabled()) {
        init_iommu(num_host_mem_channels);
    } else {
        init_hugepages(num_host_mem_channels);
    }
}

bool SysmemManager::pin_sysmem_to_device() {
    if (tt_device_->get_pci_device()->is_iommu_enabled()) {
        return pin_iommu();
    } else {
        return pin_hugepages();
    }
}

SysmemManager::~SysmemManager() {
    if (iommu_mapping != nullptr) {
        // This means we have initialized IOMMU mapping, and need to unmap it.
        // It also means that hugepage_mappings are faked, so don't unmap them.
        munmap(iommu_mapping, iommu_mapping_size);
    } else {
        for (const auto &hugepage_mapping : hugepage_mapping_per_channel) {
            if (hugepage_mapping.mapping) {
                munmap(hugepage_mapping.mapping, hugepage_mapping.mapping_size);
            }
        }
    }
}

void SysmemManager::write_to_sysmem(uint16_t channel, const void *src, uint64_t sysmem_dest, uint32_t size) {
    hugepage_mapping hugepage_map = get_hugepage_mapping(channel);
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
        LogSiliconDriver,
        "Using hugepage mapping at address {:p} offset {} chan {} size {}",
        hugepage_map.mapping,
        (sysmem_dest % hugepage_map.mapping_size),
        channel,
        size);
    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_dest % hugepage_map.mapping_size);

    memcpy(user_scratchspace, src, size);
}

void SysmemManager::read_from_sysmem(uint16_t channel, void *dest, uint64_t sysmem_src, uint32_t size) {
    hugepage_mapping hugepage_map = get_hugepage_mapping(channel);
    TT_ASSERT(
        hugepage_map.mapping,
        "read_buffer: Hugepages are not allocated for pci device num: {} ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        tt_device_->get_pci_device()->get_device_num(),
        channel);

    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_src % hugepage_map.mapping_size);

    log_debug(
        LogSiliconDriver,
        "Cluster::read_buffer (pci device num: {}, ch: {}) from {:p}",
        tt_device_->get_pci_device()->get_device_num(),
        channel,
        user_scratchspace);

    memcpy(dest, user_scratchspace, size);
}

bool SysmemManager::init_hugepages(uint32_t num_host_mem_channels) {
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
        LogSiliconDriver,
        "Using {} Hugepages/NumHostMemChannels for PCIDevice {}",
        num_host_mem_channels,
        tt_device_->get_pci_device()->get_device_num());

    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;
    auto physical_device_id = tt_device_->get_pci_device()->get_device_num();

    std::string hugepage_dir = find_hugepage_dir(hugepage_size);
    if (hugepage_dir.empty()) {
        log_warning(
            LogSiliconDriver,
            "SysmemManager::init_hugepage: no huge page mount found for hugepage_size: {}.",
            hugepage_size);
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
                LogSiliconDriver,
                "SysmemManager::init_hugepage: physical_device_id: {} ch: {} creating hugepage mapping file failed.",
                physical_device_id,
                ch);
            success = false;
            continue;
        }

        // Verify opened file size.
        struct stat hugepage_st;
        if (fstat(hugepage_fd, &hugepage_st) == -1) {
            log_warning(LogSiliconDriver, "Error reading hugepage file size after opening.");
        }

        std::byte *mapping = static_cast<std::byte *>(
            mmap(nullptr, hugepage_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, hugepage_fd, 0));

        close(hugepage_fd);

        if (mapping == MAP_FAILED) {
            log_warning(
                LogSiliconDriver,
                "UMD: Mapping a hugepage failed. (device: {}, {}/{} errno: {}).",
                physical_device_id,
                ch,
                num_host_mem_channels,
                strerror(errno));
            if (hugepage_st.st_size == 0) {
                log_warning(
                    LogSiliconDriver,
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
        if (!tt::cpuset::tt_cpuset_allocator::bind_area_to_memory_nodeset(physical_device_id, mapping, hugepage_size)) {
            log_warning(
                LogSiliconDriver,
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

bool SysmemManager::pin_hugepages() {
    auto physical_device_id = tt_device_->get_pci_device()->get_device_num();

    bool success = true;

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
        void *mapping = hugepage_mapping_per_channel.at(ch).mapping;
        size_t hugepage_size = hugepage_mapping_per_channel.at(ch).mapping_size;
        size_t actual_size = (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && ch == 3)
                                 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT
                                 : hugepage_size;
        uint64_t physical_address = tt_device_->get_pci_device()->map_for_hugepage(mapping, actual_size);

        if (physical_address == 0) {
            log_warning(
                LogSiliconDriver,
                "---- ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} TENSTORRENT_IOCTL_PIN_PAGES failed "
                "(errno: {}). Common Issue: Requires TTMKD >= 1.11, see following file contents...",
                physical_device_id,
                ch,
                strerror(errno));
            munmap(mapping, hugepage_size);
            print_file_contents("/sys/module/tenstorrent/version", "(TTKMD version)");
            print_file_contents("/proc/meminfo");
            print_file_contents("/proc/buddyinfo");
            success = false;
            continue;
        }

        hugepage_mapping_per_channel.at(ch).physical_address = physical_address;

        log_debug(
            LogSiliconDriver,
            "ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} mapping_size: {} physical address 0x{:x}",
            physical_device_id,
            ch,
            hugepage_size,
            physical_address);
    }

    return success;
}

bool SysmemManager::init_iommu(uint32_t num_host_mem_channels) {
    if (num_host_mem_channels == 0) {
        // No fake hugepage channels requested, so just skip initialization.
        return true;
    }
    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;
    size_t size = hugepage_size * num_host_mem_channels;

    constexpr size_t carveout_size = HUGEPAGE_REGION_SIZE - HUGEPAGE_CHANNEL_3_SIZE_LIMIT;  // 1GB - 768MB = 256MB

    // Caclulate the size of the mapping in order to avoid overlap with PCIE registers.
    iommu_mapping_size =
        (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && num_host_mem_channels == 4) ? (size - carveout_size) : size;

    if (!tt_device_->get_pci_device()->is_iommu_enabled()) {
        TT_THROW("IOMMU is required for sysmem without hugepages.");
    }

    log_info(LogSiliconDriver, "Allocating sysmem without hugepages (size: {:#x}).", iommu_mapping_size);
    iommu_mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    if (iommu_mapping == MAP_FAILED) {
        TT_THROW(
            "UMD: Failed to allocate memory for device/host shared buffer (size: {} errno: {}).",
            size,
            strerror(errno));
    }

    hugepage_mapping_per_channel.resize(num_host_mem_channels);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (size_t ch = 0; ch < num_host_mem_channels; ch++) {
        uint8_t *fake_mapping = static_cast<uint8_t *>(iommu_mapping) + ch * HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel[ch] = {fake_mapping, HUGEPAGE_REGION_SIZE, 0};
    }

    return true;
}

bool SysmemManager::pin_iommu() {
    if (iommu_mapping == nullptr) {
        // No fake hugepage channels requested, so just skip mapping.
        return true;
    }
    sysmem_buffer_ = map_sysmem_buffer(iommu_mapping, iommu_mapping_size);
    uint64_t iova = sysmem_buffer_->get_device_io_addr();

    log_info(LogSiliconDriver, "Mapped sysmem without hugepages to IOVA {:#x}.", iova);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (size_t ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
        uint64_t device_io_address = iova + ch * HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel.at(ch).physical_address = device_io_address;
    }

    return true;
}

size_t SysmemManager::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

hugepage_mapping SysmemManager::get_hugepage_mapping(size_t channel) const {
    if (hugepage_mapping_per_channel.size() <= channel) {
        return {nullptr, 0, 0};
    } else {
        return hugepage_mapping_per_channel[channel];
    }
}

void SysmemManager::print_file_contents(std::string filename, std::string hint) {
    if (std::filesystem::exists(filename)) {
        std::ifstream meminfo(filename);
        if (meminfo.is_open()) {
            std::cout << std::endl << "File " << filename << " " << hint << " is: " << std::endl;
            std::cout << meminfo.rdbuf();
        }
    }
}

std::unique_ptr<SysmemBuffer> SysmemManager::allocate_sysmem_buffer(size_t sysmem_buffer_size) {
    void *mapping =
        mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    return map_sysmem_buffer(mapping, sysmem_buffer_size);
}

std::unique_ptr<SysmemBuffer> SysmemManager::map_sysmem_buffer(void *buffer, size_t sysmem_buffer_size) {
    return std::make_unique<SysmemBuffer>(tlb_manager_, buffer, sysmem_buffer_size);
}

}  // namespace tt::umd
