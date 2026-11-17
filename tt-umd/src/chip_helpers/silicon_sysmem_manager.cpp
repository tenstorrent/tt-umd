// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"

#include <fmt/format.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "hugepage.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Channel 3 is shortened so the four channels leave the top of a 4 GiB device window free; the size of every other
// channel is a whole 1 GiB hugepage.
size_t channel_size(uint32_t channel) {
    return channel == 3 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT : static_cast<size_t>(HUGEPAGE_REGION_SIZE);
}

size_t page_size() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }

// Device code addresses channel N at pcie_base + N GiB, so a channel the KMD placed elsewhere is unusable.
[[noreturn]] void throw_misplaced_channel(
    uint32_t channel, int communication_id, uint64_t noc_address, uint64_t expected_noc_address) {
    UMD_THROW(
        error::RuntimeError,
        fmt::format(
            "Host memory channel {} of device {} was mapped at NOC address {:#x} instead of {:#x}; another process "
            "probably holds part of the device's NOC-to-host window.",
            channel,
            communication_id,
            noc_address,
            expected_noc_address));
}

}  // namespace

SiliconSysmemManager::SiliconSysmemManager(TTDevice* tt_device, uint32_t num_host_mem_channels) {
    UMD_ASSERT(tt_device != nullptr, error::RuntimeError, "SiliconSysmemManager needs a TTDevice.");
    tt_device_ = tt_device;
    pci_device_ = tt_device->get_pci_device();
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "SiliconSysmemManager needs a PCIe device.");
    pcie_base_ = get_pcie_base_for_arch(tt_device->get_arch());
    communication_id_ = tt_device->get_communication_device_id();
    init_sysmem(num_host_mem_channels);
}

SiliconSysmemManager::~SiliconSysmemManager() {
    try {
        unpin_or_unmap_sysmem();
    } catch (const std::exception& e) {
        log_warning(tt::LogUMD, "Unpinning host memory channels failed: {}", e.what());
    }

    if (iommu_mapping != nullptr) {
        munmap(iommu_mapping, iommu_mapping_size);
    } else {
        // Hugepage channels are mapped one hugepage each.
        for (const HugepageMapping& channel : hugepage_mapping_per_channel) {
            munmap(channel.mapping, HUGEPAGE_REGION_SIZE);
        }
    }
    iommu_mapping = nullptr;
    iommu_mapping_size = 0;
    hugepage_mapping_per_channel.clear();
}

bool SiliconSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    UMD_ASSERT(
        num_host_mem_channels <= MAX_HOST_MEM_CHANNELS,
        error::RuntimeError,
        fmt::format(
            "{} host memory channels requested, at most {} are supported.",
            num_host_mem_channels,
            MAX_HOST_MEM_CHANNELS));
    if (num_host_mem_channels == 0) {
        return true;
    }
    return pci_device_->is_iommu_enabled() ? init_iommu(num_host_mem_channels) : init_hugepages(num_host_mem_channels);
}

bool SiliconSysmemManager::init_iommu(uint32_t num_fake_mem_channels) {
    // With an IOMMU any memory can be pinned, so the channels are carved out of one anonymous mapping instead of
    // needing hugepages.
    size_t total_size = 0;
    for (uint32_t channel = 0; channel < num_fake_mem_channels; channel++) {
        total_size += channel_size(channel);
    }

    void* mapping = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    UMD_ASSERT(
        mapping != MAP_FAILED,
        error::RuntimeError,
        fmt::format("Failed to allocate {:#x} bytes of host memory channels: {}", total_size, std::strerror(errno)));
    iommu_mapping = mapping;
    iommu_mapping_size = total_size;

    size_t channel_offset = 0;
    for (uint32_t channel = 0; channel < num_fake_mem_channels; channel++) {
        hugepage_mapping_per_channel.push_back(
            {static_cast<uint8_t*>(mapping) + channel_offset, channel_size(channel), /*physical_address=*/0});
        channel_offset += channel_size(channel);
    }
    log_debug(
        tt::LogUMD,
        "Allocated {} host memory channels for device {} backed by IOMMU-mapped memory.",
        num_fake_mem_channels,
        communication_id_);
    return true;
}

bool SiliconSysmemManager::init_hugepages(uint32_t num_host_mem_channels) {
    const std::string hugepage_dir = find_hugepage_dir(HUGEPAGE_REGION_SIZE);
    if (hugepage_dir.empty()) {
        log_warning(
            tt::LogUMD,
            "No 1 GiB hugepage mount found and no IOMMU is active, so device {} gets no host memory channels.",
            communication_id_);
        print_file_contents("/proc/cmdline", "The kernel command line must reserve 1 GiB hugepages.");
        return false;
    }

    const PciDeviceInfo device_info = pci_device_->get_device_info();
    const uint32_t num_channels = get_available_num_host_mem_channels(
        num_host_mem_channels, device_info.device_id, static_cast<uint16_t>(pci_device_->get_pci_revision()));

    for (uint32_t channel = 0; channel < num_channels; channel++) {
        const int fd = open_hugepage_file(hugepage_dir, pci_device_->get_device_num(), channel);
        if (fd == -1) {
            log_warning(tt::LogUMD, "Opening hugepage file for channel {} failed: {}", channel, std::strerror(errno));
            print_file_contents("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages", "1 GiB hugepages");
            return false;
        }
        // MAP_POPULATE claims the hugepage now, so a shortage shows up here rather than as SIGBUS on first touch.
        void* mapping = mmap(nullptr, HUGEPAGE_REGION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        close(fd);
        if (mapping == MAP_FAILED) {
            log_warning(tt::LogUMD, "Mapping hugepage for channel {} failed: {}", channel, std::strerror(errno));
            print_file_contents("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages", "1 GiB hugepages");
            return false;
        }
        hugepage_mapping_per_channel.push_back({mapping, channel_size(channel), /*physical_address=*/0});
    }
    log_debug(tt::LogUMD, "Allocated {} hugepage host memory channels for device {}.", num_channels, communication_id_);
    return true;
}

bool SiliconSysmemManager::pin_or_map_sysmem_to_device() {
    return pci_device_->is_iommu_enabled() ? pin_or_map_iommu() : pin_or_map_hugepages();
}

bool SiliconSysmemManager::pin_or_map_iommu() {
    for (uint32_t channel = 0; channel < hugepage_mapping_per_channel.size(); channel++) {
        HugepageMapping& mapping = hugepage_mapping_per_channel[channel];
        if (mapping.physical_address != 0) {
            continue;
        }
        const auto [noc_address, iova] = pci_device_->map_buffer_to_noc(mapping.mapping, mapping.mapping_size);
        mapping.physical_address = iova;
        const uint64_t expected_noc_address = pcie_base_ + channel * HUGEPAGE_REGION_SIZE;
        if (noc_address != expected_noc_address) {
            unpin_or_unmap_sysmem();
            throw_misplaced_channel(channel, communication_id_, noc_address, expected_noc_address);
        }
    }
    return true;
}

bool SiliconSysmemManager::pin_or_map_hugepages() {
    for (uint32_t channel = 0; channel < hugepage_mapping_per_channel.size(); channel++) {
        HugepageMapping& mapping = hugepage_mapping_per_channel[channel];
        if (mapping.physical_address != 0) {
            continue;
        }
        const auto [noc_address, physical_address] =
            pci_device_->map_hugepage_to_noc(mapping.mapping, mapping.mapping_size);
        mapping.physical_address = physical_address;
        const uint64_t expected_noc_address = pcie_base_ + channel * HUGEPAGE_REGION_SIZE;
        if (noc_address != expected_noc_address) {
            unpin_or_unmap_sysmem();
            throw_misplaced_channel(channel, communication_id_, noc_address, expected_noc_address);
        }
    }
    return true;
}

void SiliconSysmemManager::unpin_or_unmap_sysmem() {
    // A pinned channel always has a non-zero bus address: the IOVA allocator never hands out 0 and no hugepage sits at
    // physical address 0. That makes the address the record of which channels are pinned.
    for (HugepageMapping& mapping : hugepage_mapping_per_channel) {
        if (mapping.physical_address == 0) {
            continue;
        }
        mapping.physical_address = 0;
        pci_device_->unmap_for_dma(mapping.mapping, mapping.mapping_size);
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot allocate an empty system memory buffer.");
    UMD_ASSERT(
        pci_device_->is_iommu_enabled(),
        error::RuntimeError,
        "Allocating system memory buffers needs an IOMMU, since without one the device can only reach physically "
        "contiguous memory.");
    const size_t page = page_size();
    UMD_ASSERT(
        sysmem_buffer_size <= std::numeric_limits<size_t>::max() - (page - 1),
        error::RuntimeError,
        fmt::format("A {:#x}-byte system memory buffer cannot be allocated.", sysmem_buffer_size));
    const size_t mapped_size = (sysmem_buffer_size + page - 1) & ~(page - 1);

    void* memory = mmap(nullptr, mapped_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    UMD_ASSERT(
        memory != MAP_FAILED,
        error::RuntimeError,
        fmt::format("Failed to allocate a {:#x}-byte system memory buffer: {}", mapped_size, std::strerror(errno)));

    SysmemBuffer::Deleter free_memory = [mapped_size](void* aligned_start) { munmap(aligned_start, mapped_size); };
    try {
        return pin_and_wrap(memory, sysmem_buffer_size, map_to_noc, DeviceBufferAccess::READ_WRITE, free_memory);
    } catch (...) {
        free_memory(memory);
        throw;
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_ASSERT(buffer != nullptr, error::RuntimeError, "Cannot map a null system memory buffer.");
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot map an empty system memory buffer.");
    UMD_ASSERT(
        pci_device_->is_iommu_enabled(),
        error::RuntimeError,
        "Mapping a caller's buffer for the device needs an IOMMU, since without one the device can only reach "
        "physically contiguous memory.");
    UMD_ASSERT(
        device_access != DeviceBufferAccess::READ_ONLY || is_read_only_page_pinning_supported(),
        error::RuntimeError,
        "Device-read-only mappings need an IOMMU and a KMD that supports read-only page pinning.");
    return pin_and_wrap(buffer, sysmem_buffer_size, map_to_noc, device_access, /*release_backing_memory=*/{});
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::pin_and_wrap(
    void* buffer_va,
    size_t buffer_size,
    const bool map_to_noc,
    DeviceBufferAccess device_access,
    SysmemBuffer::Deleter release_backing_memory) {
    const SysmemBuffer::AlignedRange range = SysmemBuffer::page_align(buffer_va, buffer_size);

    uint64_t iova = 0;
    std::optional<uint64_t> noc_address;
    if (map_to_noc) {
        const auto [noc, dma_address] = pci_device_->map_buffer_to_noc(range.base, range.mapped_size, device_access);
        noc_address = noc + range.offset_from_base;
        iova = dma_address;
    } else {
        iova = pci_device_->map_for_dma(range.base, range.mapped_size, device_access);
    }

    // The KMD unpins by the exact range it pinned, so the deleter captures the page-rounded size.
    PCIDevice* pci_device = pci_device_;
    const size_t mapped_size = range.mapped_size;
    SysmemBuffer::Deleter deleter =
        [pci_device, mapped_size, release = std::move(release_backing_memory)](void* aligned_start) {
            try {
                pci_device->unmap_for_dma(aligned_start, mapped_size);
            } catch (const std::exception& e) {
                log_warning(tt::LogUMD, "Unpinning a system memory buffer failed: {}", e.what());
            }
            // Freed even when unpinning failed: a still-pinned page stays held by the kernel, never reused under the
            // device.
            if (release) {
                release(aligned_start);
            }
        };

    // The KMD only grants a NOC mapping at pin time, so a buffer pinned without one cannot be bound later.
    return create_buffer(
        tt_device_, buffer_va, buffer_size, iova, communication_id_, std::move(deleter), noc_address, device_access);
}

void SiliconSysmemManager::print_file_contents(const std::string& filename, const std::string& hint) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        log_warning(tt::LogUMD, "Could not open {} ({}).", filename, hint);
        return;
    }
    std::stringstream contents;
    contents << file.rdbuf();
    log_warning(tt::LogUMD, "{} ({}): {}", filename, hint, contents.str());
}

}  // namespace tt::umd
