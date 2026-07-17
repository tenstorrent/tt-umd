/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"

#include <fmt/format.h>
#include <linux/mman.h>  // for MAP_HUGE_1GB, MAP_HUGE_512MB, MAP_HUGE_2MB
#include <sys/mman.h>    // for mmap, munmap
#include <sys/stat.h>    // for fstat
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <vector>

#include "cpuset_lib.hpp"
#include "hugepage.hpp"
#include "tracy.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// Try hugepage sizes from largest to smallest, then fall back to regular pages.
// Larger hugepages are strictly preferred: each hugepage maps to one SMMU TLB entry
// regardless of size, so 1GB (PUD/level-1 block) consumes half as many TLB entries as
// 512MB (PMD/level-2 block) for the same total memory, reducing IOMMU mapping overhead.
// 512MB is only meaningful on AArch64 with 64K base pages; it is not exposed on x86.
static void *mmap_with_hugepage_fallback(size_t size) {
    constexpr size_t kHugepage1GiB = 1ULL << 30;
    constexpr size_t kHugepage512MiB = 512ULL << 20;
    constexpr size_t kHugepage2MiB = 2ULL << 20;

    void *addr = MAP_FAILED;

    // Only attempt 1GiB hugepages when the size is a multiple of 1GiB.
    if (size >= kHugepage1GiB && (size % kHugepage1GiB) == 0) {
        addr = mmap(
            nullptr,
            size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB | MAP_POPULATE,
            -1,
            0);
        if (addr != MAP_FAILED) {
            log_debug(LogUMD, "Allocated {:#x} bytes using 1GB hugepages.", size);
            return addr;
        }
    }

    // 512MB hugepages (PMD block on AArch64 with 64K base pages).
    if (size >= kHugepage512MiB && (size % kHugepage512MiB) == 0) {
        addr = mmap(
            nullptr,
            size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_512MB | MAP_POPULATE,
            -1,
            0);
        if (addr != MAP_FAILED) {
            log_debug(LogUMD, "Allocated {:#x} bytes using 512MB hugepages.", size);
            return addr;
        }
    }

    // Only attempt 2MiB hugepages when the size is a multiple of 2MiB.
    if (size >= kHugepage2MiB && (size % kHugepage2MiB) == 0) {
        addr = mmap(
            nullptr,
            size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB | MAP_POPULATE,
            -1,
            0);
        if (addr != MAP_FAILED) {
            log_debug(LogUMD, "Allocated {:#x} bytes using 2MB hugepages.", size);
            return addr;
        }
    }

    addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (addr != MAP_FAILED) {
        log_warning(
            LogUMD,
            "Sysmem ({:#x} bytes) using regular pages; pre-allocate hugepages for better DMA performance "
            "(e.g. {} × 1GB or {} × 2MB; on AArch64 also {} × 512MB).",
            size,
            (size + kHugepage1GiB - 1) / kHugepage1GiB,
            (size + kHugepage2MiB - 1) / kHugepage2MiB,
            (size + kHugepage512MiB - 1) / kHugepage512MiB);
    }
    return addr;
}

SiliconSysmemManager::SiliconSysmemManager(TTDevice *tt_device, uint32_t num_host_mem_channels) {
    tt_device_ = tt_device;
    pci_device_ = tt_device->get_pci_device();
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "PCI device not available in TTDevice.");
    pcie_base_ = get_pcie_base_for_arch(pci_device_->get_arch());
    UMD_ASSERT(
        num_host_mem_channels <= 4,
        error::RuntimeError,
        fmt::format("Only 4 host memory channels are supported per device, but {} requested.", num_host_mem_channels));

    SiliconSysmemManager::init_sysmem(num_host_mem_channels);
}

bool SiliconSysmemManager::pin_or_map_sysmem_to_device() {
    ZoneScopedC(tracy::Color::Yellow);
    if (pci_device_->is_iommu_enabled()) {
        return pin_or_map_iommu();
    } else {
        return pin_or_map_hugepages();
    }
}

SiliconSysmemManager::~SiliconSysmemManager() { SiliconSysmemManager::unpin_or_unmap_sysmem(); }

bool SiliconSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    ZoneScopedC(tracy::Color::Yellow);
    if (pci_device_->is_iommu_enabled()) {
        return init_iommu(num_host_mem_channels);
    } else {
        return init_hugepages(num_host_mem_channels);
    }
}

void SiliconSysmemManager::unpin_or_unmap_sysmem() {
    ZoneScopedC(tracy::Color::Yellow);
    // This will unmap the iommu buffer if it was mapped through kmd.
    sysmem_buffer_.reset();
    if (iommu_mapping != nullptr) {
        // This means we have initialized IOMMU mapping, and need to unmap it.
        // It also means that HugepageMappings are faked, so don't unmap them.
        TracyFreeN(iommu_mapping, "Sysmem");
        munmap(iommu_mapping, iommu_mapping_size);
        iommu_mapping = nullptr;
    } else {
        for (int ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
            auto &hugepage_mapping = hugepage_mapping_per_channel[ch];
            if (hugepage_mapping.physical_address && pci_device_->is_mapping_buffer_to_noc_supported()) {
                // This will unmap the hugepage if it was mapped through kmd.
                // This is a hack for the 4th hugepage channel which is limited to 768MB.
                size_t actual_size = (pci_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && ch == 3)
                                         ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT
                                         : hugepage_mapping.mapping_size;
                pci_device_->unmap_for_dma(hugepage_mapping.mapping, actual_size);
            }
            if (hugepage_mapping.mapping) {
                // Note that we mmap full hugepage, but don't map it filly to NOC.
                // So the hack for 4th hugepage channel is not present in this branch.
                TracyFreeN(hugepage_mapping.mapping, "Hugepage");
                munmap(hugepage_mapping.mapping, hugepage_mapping.mapping_size);
            }
        }
    }
    hugepage_mapping_per_channel.clear();
}

bool SiliconSysmemManager::init_hugepages(uint32_t num_host_mem_channels) {
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
    uint16_t pcie_device_id = pci_device_->get_pci_device_id();
    uint32_t pcie_revision = pci_device_->get_pci_revision();
    num_host_mem_channels = get_available_num_host_mem_channels(num_host_mem_channels, pcie_device_id, pcie_revision);

    log_debug(
        LogUMD,
        "Using {} Hugepages/NumHostMemChannels for PCIDevice {}",
        num_host_mem_channels,
        pci_device_->get_device_num());

    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;
    auto physical_device_id = pci_device_->get_device_num();

    std::string hugepage_dir = find_hugepage_dir(hugepage_size);
    if (hugepage_dir.empty()) {
        log_warning(
            LogUMD,
            "SiliconSysmemManager::init_hugepage: no huge page mount found for hugepage_size: {}.",
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
                LogUMD,
                "SiliconSysmemManager::init_hugepage: physical_device_id: {} ch: {} creating hugepage mapping file "
                "failed.",
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
        TracyAllocN(mapping, hugepage_size, "Hugepage");
    }

    return success;
}

bool SiliconSysmemManager::pin_or_map_hugepages() {
    auto physical_device_id = pci_device_->get_device_num();

    bool success = true;

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
        void *mapping = hugepage_mapping_per_channel.at(ch).mapping;
        size_t hugepage_size = hugepage_mapping_per_channel.at(ch).mapping_size;
        size_t actual_size = (pci_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && ch == 3)
                                 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT
                                 : hugepage_size;
        bool map_buffer_to_noc = pci_device_->is_mapping_buffer_to_noc_supported();
        uint64_t physical_address;
        uint64_t noc_address;
        if (map_buffer_to_noc) {
            std::tie(noc_address, physical_address) = pci_device_->map_hugepage_to_noc(mapping, actual_size);
            uint64_t expected_noc_address = pcie_base_ + (ch * hugepage_size);

            log_debug(LogUMD, "Mapped hugepage {:#x} to NOC address {:#x}", physical_address, noc_address);
            // Note that the truncated page is the final one, so there is no need to
            // give expected_noc_address special treatment for a subsequent page.
            if (noc_address != expected_noc_address) {
                log_warning(
                    LogUMD,
                    "NOC address of a hugepage does not match the expected address. This usually means another "
                    "process is already holding the sysmem NOC address space UMD requires (often a stale or crashed "
                    "process from a previous run). To fix this, find and kill the other processes using the "
                    "Tenstorrent device(s), then retry. Proceeding could lead to undefined behavior.");
            }
        } else {
            physical_address = pci_device_->map_for_hugepage(mapping, actual_size);
        }

        if (physical_address == 0) {
            log_warning(
                LogUMD,
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
            LogUMD,
            "ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} mapping_size: {} physical address 0x{:x}",
            physical_device_id,
            ch,
            hugepage_size,
            physical_address);
    }

    return success;
}

bool SiliconSysmemManager::init_iommu(uint32_t num_fake_mem_channels) {
    if (num_fake_mem_channels == 0) {
        // No fake hugepage channels requested, so just skip initialization.
        return true;
    }

    constexpr size_t carveout_size = HUGEPAGE_REGION_SIZE - HUGEPAGE_CHANNEL_3_SIZE_LIMIT;  // 1GB - 768MB = 256MB
    const size_t size = num_fake_mem_channels * HUGEPAGE_REGION_SIZE;

    // Caclulate the size of the mapping in order to avoid overlap with PCIE registers on WH.
    if (pci_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && num_fake_mem_channels == 4) {
        iommu_mapping_size = (num_fake_mem_channels == 4) ? (size - carveout_size) : size;
    } else {
        iommu_mapping_size = size;
    }

    if (!pci_device_->is_iommu_enabled()) {
        UMD_THROW(error::RuntimeError, "IOMMU is required for sysmem without hugepages.");
    }

    log_debug(LogUMD, "Allocating sysmem for IOMMU (size: {:#x}).", iommu_mapping_size);
    iommu_mapping = mmap_with_hugepage_fallback(size);

    if (iommu_mapping == MAP_FAILED) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "UMD: Failed to allocate memory for device/host shared buffer (size: {} errno: {}).",
                size,
                strerror(errno)));
    }
    TracyAllocN(iommu_mapping, iommu_mapping_size, "Sysmem");

    hugepage_mapping_per_channel.resize(num_fake_mem_channels);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (size_t ch = 0; ch < num_fake_mem_channels; ch++) {
        uint8_t *fake_mapping = static_cast<uint8_t *>(iommu_mapping) + ch * HUGEPAGE_REGION_SIZE;
        size_t actual_size = (pci_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && ch == 3)
                                 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT
                                 : HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel[ch] = {fake_mapping, actual_size, 0};
    }

    return true;
}

bool SiliconSysmemManager::pin_or_map_iommu() {
    if (iommu_mapping == nullptr) {
        // No fake hugepage channels requested, so just skip mapping.
        return true;
    }

    bool map_buffer_to_noc = pci_device_->is_mapping_buffer_to_noc_supported();

    sysmem_buffer_ = map_sysmem_buffer(iommu_mapping, iommu_mapping_size, map_buffer_to_noc);
    uint64_t iova = sysmem_buffer_->get_device_io_addr();
    auto noc_address = sysmem_buffer_->get_noc_addr();

    if (map_buffer_to_noc && !noc_address.has_value()) {
        UMD_THROW(error::RuntimeError, "NOC address is not set for sysmem buffer.");
    }

    if (map_buffer_to_noc && (*noc_address != pcie_base_)) {
        // If this happens, it means that something else is using the address
        // space that UMD typically uses.  Historically, this would have crashed
        // or done something inscrutable.  Now it is just an error.
        //
        // The usual cause is a stale process (a leftover/crashed run) that still
        // holds a sysmem mapping at the NOC base address, so this process gets
        // bumped to a different address. The fix is to find and kill those
        // processes, then retry.
        log_error(
            LogUMD,
            "Expected sysmem to be mapped at NOC address {:#x}, but it was mapped at {:#x}. This usually means "
            "another process is already holding the sysmem NOC address space UMD requires (often a stale or "
            "crashed process from a previous run). To fix this, find and kill the other processes using the "
            "Tenstorrent device(s), then retry.",
            pcie_base_,
            *noc_address);
        UMD_THROW(
            error::RuntimeError,
            "Sysmem mapped at unexpected NOC address (likely a stale process holding sysmem); proceeding could "
            "lead to undefined behavior");
    }

    if (map_buffer_to_noc) {
        log_debug(LogUMD, "Mapped sysmem via IOMMU to IOVA {:#x}; NOC address {:#x}", iova, *noc_address);
    } else {
        log_debug(LogUMD, "Mapped sysmem via IOMMU to IOVA {:#x}", iova);
    }

    for (size_t ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
        uint64_t device_io_address = iova + ch * HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel.at(ch).physical_address = device_io_address;
    }

    return true;
}

void SiliconSysmemManager::print_file_contents(const std::string &filename, const std::string &hint) {
    if (std::filesystem::exists(filename)) {
        std::ifstream meminfo(filename);
        if (meminfo.is_open()) {
            std::cout << std::endl << "File " << filename << " " << hint << " is: " << std::endl;
            std::cout << meminfo.rdbuf();
        }
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    ZoneScopedC(tracy::Color::Yellow);
    void *mapping = mmap_with_hugepage_fallback(sysmem_buffer_size);
    if (mapping == MAP_FAILED) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to allocate sysmem buffer of size {:#x} bytes with mmap.", sysmem_buffer_size));
    }
    return map_sysmem_buffer(mapping, sysmem_buffer_size, map_to_noc);
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess access) {
    log_debug(LogUMD, "Mapping sysmem buffer to NOC: {:#x}", sysmem_buffer_size);
    return std::make_unique<SysmemBuffer>(tt_device_, buffer, sysmem_buffer_size, map_to_noc, access);
}

}  // namespace tt::umd
