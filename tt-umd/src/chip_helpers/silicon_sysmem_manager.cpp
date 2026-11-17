// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"

#include <fmt/format.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "hugepage.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Channel i sits at i GiB in the device's view of host memory; only the last of the four channels is cut short.
uint64_t channel_size(uint32_t channel) {
    return channel == MAX_HOST_MEM_CHANNELS - 1 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT : HUGEPAGE_REGION_SIZE;
}

}  // namespace

SiliconSysmemManager::SiliconSysmemManager(TTDevice* tt_device, uint32_t num_host_mem_channels) {
    UMD_ASSERT(tt_device != nullptr, error::RuntimeError, "SiliconSysmemManager needs a TTDevice.");
    tt_device_ = tt_device;
    pci_device_ = tt_device->get_pci_device();
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "SiliconSysmemManager needs a PCIe-attached TTDevice.");
    pcie_base_ = get_pcie_base_for_arch(tt_device->get_arch());
    communication_id_ = tt_device->get_communication_device_id();
    init_sysmem(num_host_mem_channels);
}

SiliconSysmemManager::~SiliconSysmemManager() {
    try {
        unpin_or_unmap_sysmem();
    } catch (const std::exception& e) {
        log_error(LogUMD, "Unpinning host memory of device {} failed: {}", communication_id_, e.what());
    }
    if (iommu_mapping != nullptr) {
        munmap(iommu_mapping, iommu_mapping_size);
        iommu_mapping = nullptr;
    }
    // Without an IOMMU each channel is its own hugepage mapping; with one they are views into iommu_mapping.
    if (!pci_device_->is_iommu_enabled()) {
        for (const HugepageMapping& channel : hugepage_mapping_per_channel) {
            munmap(channel.mapping, HUGEPAGE_REGION_SIZE);
        }
    }
    hugepage_mapping_per_channel.clear();
}

bool SiliconSysmemManager::pin_or_map_sysmem_to_device() {
    if (hugepage_mapping_per_channel.empty()) {
        return true;
    }
    return pci_device_->is_iommu_enabled() ? pin_or_map_iommu() : pin_or_map_hugepages();
}

void SiliconSysmemManager::unpin_or_unmap_sysmem() {
    // With an IOMMU the channels' pin is owned by sysmem_buffer_, whose deleter unpins.
    sysmem_buffer_.reset();
    for (HugepageMapping& channel : hugepage_mapping_per_channel) {
        // A hugepage never sits at physical address 0, so 0 marks a channel that is not pinned.
        if (!pci_device_->is_iommu_enabled() && channel.physical_address != 0) {
            pci_device_->unmap_for_dma(channel.mapping, channel.mapping_size);
        }
        channel.physical_address = 0;
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot allocate an empty sysmem buffer.");
    UMD_ASSERT(
        pci_device_->is_iommu_enabled(),
        error::RuntimeError,
        "Allocating sysmem buffers requires an IOMMU; without one only the hugepage channels are device-visible.");
    void* memory = mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    UMD_ASSERT(
        memory != MAP_FAILED,
        error::RuntimeError,
        fmt::format("Allocating a sysmem buffer of {} bytes failed: {}", sysmem_buffer_size, std::strerror(errno)));

    // mmap returns page-aligned memory and backs whole pages, so the aligned extent is exactly what was mapped.
    const size_t mapped_size = SysmemBuffer::page_align(memory, sysmem_buffer_size).mapped_size;
    SysmemBuffer::Deleter release = [mapped_size](void* base) { munmap(base, mapped_size); };
    try {
        return pin_and_wrap(memory, sysmem_buffer_size, map_to_noc, DeviceBufferAccess::READ_WRITE, release);
    } catch (...) {
        release(memory);
        throw;
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_ASSERT(buffer != nullptr, error::RuntimeError, "Cannot map a null sysmem buffer.");
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot map an empty sysmem buffer.");
    UMD_ASSERT(
        pci_device_->is_iommu_enabled(),
        error::RuntimeError,
        "Mapping a user buffer for the device requires an IOMMU.");
    return pin_and_wrap(buffer, sysmem_buffer_size, map_to_noc, device_access, SysmemBuffer::Deleter{});
}

bool SiliconSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    UMD_ASSERT(
        num_host_mem_channels <= MAX_HOST_MEM_CHANNELS,
        error::RuntimeError,
        fmt::format(
            "At most {} host memory channels are supported, {} requested.",
            MAX_HOST_MEM_CHANNELS,
            num_host_mem_channels));
    if (num_host_mem_channels == 0) {
        return true;
    }
    return pci_device_->is_iommu_enabled() ? init_iommu(num_host_mem_channels) : init_hugepages(num_host_mem_channels);
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::pin_and_wrap(
    void* buffer_va,
    size_t buffer_size,
    const bool map_to_noc,
    DeviceBufferAccess device_access,
    SysmemBuffer::Deleter release_backing_memory) {
    const SysmemBuffer::AlignedRange aligned = SysmemBuffer::page_align(buffer_va, buffer_size);

    uint64_t iova = 0;
    std::optional<uint64_t> noc_addr;
    if (map_to_noc) {
        const auto [noc_address, dma_address] =
            pci_device_->map_buffer_to_noc(aligned.base, aligned.mapped_size, device_access);
        iova = dma_address;
        // The KMD reports the NOC address of the aligned start; the buffer's NOC address names the user's first byte.
        noc_addr = noc_address + aligned.offset_from_base;
    } else {
        iova = pci_device_->map_for_dma(aligned.base, aligned.mapped_size, device_access);
    }

    SysmemBuffer::Deleter deleter = [pci_device = pci_device_,
                                     mapped_size = aligned.mapped_size,
                                     release = std::move(release_backing_memory)](void* aligned_base) {
        // Unpinned first, so the device never holds a mapping to freed pages.
        pci_device->unmap_for_dma(aligned_base, mapped_size);
        if (release) {
            release(aligned_base);
        }
    };

    log_trace(
        LogUMD,
        "Pinned sysmem buffer {} ({} bytes) for device {} at IOVA {:#x}.",
        buffer_va,
        buffer_size,
        communication_id_,
        iova);
    // The KMD assigns a NOC address only when pinning, so a buffer pinned without one cannot be bound later.
    return create_buffer(
        tt_device_, buffer_va, buffer_size, iova, communication_id_, std::move(deleter), noc_addr, device_access);
}

bool SiliconSysmemManager::init_hugepages(uint32_t num_host_mem_channels) {
    const uint32_t num_channels = get_available_num_host_mem_channels(
        num_host_mem_channels,
        static_cast<uint16_t>(pci_device_->get_pci_device_id()),
        static_cast<uint16_t>(pci_device_->get_pci_revision()));

    const std::string hugepage_dir = find_hugepage_dir(HUGEPAGE_REGION_SIZE);
    if (hugepage_dir.empty()) {
        log_warning(
            LogUMD, "No 1 GiB hugepage mount found, device {} gets no host memory channels.", communication_id_);
        print_file_contents("/proc/cmdline", "Check that the kernel is booted with 1 GiB hugepages.");
        return false;
    }

    for (uint32_t channel = 0; channel < num_channels; channel++) {
        const int fd = open_hugepage_file(hugepage_dir, pci_device_->get_device_num(), channel);
        if (fd == -1) {
            log_warning(
                LogUMD, "Opening the hugepage file for device {} channel {} failed.", communication_id_, channel);
            return false;
        }
        void* mapping = mmap(nullptr, HUGEPAGE_REGION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        close(fd);
        if (mapping == MAP_FAILED) {
            log_warning(
                LogUMD,
                "Mapping the hugepage for device {} channel {} failed: {}",
                communication_id_,
                channel,
                std::strerror(errno));
            print_file_contents(
                "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages",
                "Each host memory channel of each device needs one free 1 GiB hugepage.");
            return false;
        }
        hugepage_mapping_per_channel.push_back(HugepageMapping{mapping, channel_size(channel), 0});
    }
    return true;
}

bool SiliconSysmemManager::init_iommu(uint32_t num_fake_mem_channels) {
    const uint64_t total_size =
        (num_fake_mem_channels - 1) * HUGEPAGE_REGION_SIZE + channel_size(num_fake_mem_channels - 1);
    // Pages are faulted in when the channels are pinned, so construction stays cheap for a chip never started.
    void* memory = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    UMD_ASSERT(
        memory != MAP_FAILED,
        error::RuntimeError,
        fmt::format(
            "Mapping {} host memory channels for device {} failed: {}",
            num_fake_mem_channels,
            communication_id_,
            std::strerror(errno)));

    iommu_mapping = memory;
    iommu_mapping_size = total_size;
    for (uint32_t channel = 0; channel < num_fake_mem_channels; channel++) {
        hugepage_mapping_per_channel.push_back(
            HugepageMapping{static_cast<uint8_t*>(memory) + channel * HUGEPAGE_REGION_SIZE, channel_size(channel), 0});
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
        // Device software reaches channel i at a fixed NOC address, so a channel placed elsewhere in the
        // aperture (another client holding the bottom of it) would be silently unreachable.
        const uint64_t expected_noc_address = pcie_base_ + channel * HUGEPAGE_REGION_SIZE;
        UMD_ASSERT(
            noc_address == expected_noc_address,
            error::RuntimeError,
            fmt::format(
                "Host memory channel {} of device {} was mapped at NOC address {:#x} instead of {:#x}; another "
                "process may be holding the device's host memory aperture.",
                channel,
                communication_id_,
                noc_address,
                expected_noc_address));
    }
    return true;
}

bool SiliconSysmemManager::pin_or_map_iommu() {
    if (sysmem_buffer_ != nullptr) {
        return true;
    }
    // All channels are pinned as one buffer, so they are contiguous both in IOVA and in the NOC aperture.
    std::unique_ptr<SysmemBuffer> channels = pin_and_wrap(
        iommu_mapping,
        iommu_mapping_size,
        /*map_to_noc=*/true,
        DeviceBufferAccess::READ_WRITE,
        SysmemBuffer::Deleter{});
    const uint64_t noc_address = channels->get_noc_address().value();
    UMD_ASSERT(
        noc_address == pcie_base_,
        error::RuntimeError,
        fmt::format(
            "Host memory channels of device {} were mapped at NOC address {:#x} instead of {:#x}; another process "
            "may be holding the device's host memory aperture.",
            communication_id_,
            noc_address,
            pcie_base_));

    for (uint32_t channel = 0; channel < hugepage_mapping_per_channel.size(); channel++) {
        hugepage_mapping_per_channel[channel].physical_address = channels->get_iova(channel * HUGEPAGE_REGION_SIZE);
    }
    sysmem_buffer_ = std::move(channels);
    log_debug(
        LogUMD,
        "Pinned {} host memory channels of device {} at NOC address {:#x}.",
        hugepage_mapping_per_channel.size(),
        communication_id_,
        noc_address);
    return true;
}

void SiliconSysmemManager::print_file_contents(const std::string& filename, const std::string& hint) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    log_warning(LogUMD, "{}: {} {}", filename, contents, hint);
}

}  // namespace tt::umd
