/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"

#include <fmt/format.h>
#include <linux/mman.h>  // for MAP_HUGE_1GB, MAP_HUGE_512MB, MAP_HUGE_2MB
#include <sys/mman.h>    // for mmap, munmap
#include <sys/stat.h>    // for fstat
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "cpuset_lib.hpp"
#include "hugepage.hpp"
#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_types.hpp"
#include "tt-umd/utils/error.hpp"

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

// The page-aligned extent covering a buffer. Pinning works on whole pages, so an unaligned start is
// rounded down and the size grown to cover the user's whole range.
struct AlignedRange {
    void *base = nullptr;           // What gets pinned and released.
    size_t mapped_size = 0;         // Page-rounded size covering the user's whole range.
    uint64_t offset_from_base = 0;  // Distance from base to the user's virtual address.
};

static AlignedRange page_align(void *buffer_va, size_t buffer_size) {
    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t va = reinterpret_cast<uint64_t>(buffer_va);
    const uint64_t base = va & ~(page_size - 1);
    AlignedRange range{};
    range.base = reinterpret_cast<void *>(base);
    range.offset_from_base = va - base;
    range.mapped_size = (buffer_size + range.offset_from_base + page_size - 1) & ~(page_size - 1);
    return range;
}

// Unpins the pages on destruction unless disarmed.
//
// It covers the whole window between pinning and a SysmemBuffer taking ownership, not just the
// construction call: composing the deleter allocates a std::function, so it can throw before any
// try block is entered, and an escape there would leave the pages pinned with nothing owning them.
class UnpinGuard {
public:
    UnpinGuard(PCIDevice *pci_device, void *base, size_t size) : pci_device_(pci_device), base_(base), size_(size) {}

    UnpinGuard(const UnpinGuard &) = delete;
    UnpinGuard &operator=(const UnpinGuard &) = delete;

    ~UnpinGuard() {
        if (!armed_) {
            return;
        }
        try {
            pci_device_->unmap_for_dma(base_, size_);
        } catch (...) {
            log_warning(LogUMD, "Failed to unmap sysmem buffer after a failed construction.");
        }
    }

    void disarm() { armed_ = false; }

private:
    PCIDevice *pci_device_;
    void *base_;
    size_t size_;
    bool armed_ = true;
};

// Hugepage files are named by index, and a file keeps its page reserved after it is unmapped, even across
// runs. Handing out the lowest free index makes every buffer reuse the same few files.
struct SiliconSysmemManager::HugepageFiles {
    uint16_t acquire() {
        std::lock_guard<std::mutex> lock(mutex);
        const auto free_index = std::find(in_use.begin(), in_use.end(), false);
        const size_t index = std::distance(in_use.begin(), free_index);
        if (free_index == in_use.end()) {
            in_use.push_back(true);
        } else {
            *free_index = true;
        }
        return static_cast<uint16_t>(index);
    }

    void release(uint16_t index) {
        std::lock_guard<std::mutex> lock(mutex);
        in_use.at(index) = false;
    }

    std::mutex mutex;
    std::vector<bool> in_use;
};

SiliconSysmemManager::SiliconSysmemManager(TTDevice *tt_device, uint32_t num_host_mem_channels) :
    hugepage_files_(std::make_unique<HugepageFiles>()) {
    tt_device_ = tt_device;
    pci_device_ = tt_device->get_pci_device();
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "PCI device not available in TTDevice.");
    communication_id_ = tt_device->get_communication_device_id();
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
            if (hugepage_mapping.physical_address) {
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
            hugepage_files_->release(ch);
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
        // Nothing else holds a hugepage file yet, so channel N gets file N.
        const uint16_t index = hugepage_files_->acquire();
        void *mapping = map_hugepage(hugepage_dir, index);
        if (mapping == nullptr) {
            success = false;
            continue;
        }
        hugepage_mapping_per_channel[ch] = {mapping, hugepage_size, 0};
    }

    return success;
}

void *SiliconSysmemManager::map_hugepage(const std::string &hugepage_dir, uint16_t index) {
    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;
    auto physical_device_id = pci_device_->get_device_num();

    int hugepage_fd = open_hugepage_file(hugepage_dir, physical_device_id, index);
    if (hugepage_fd == -1) {
        // Probably a permissions problem.
        log_warning(
            LogUMD,
            "SiliconSysmemManager::init_hugepage: physical_device_id: {} ch: {} creating hugepage mapping file "
            "failed.",
            physical_device_id,
            index);
        return nullptr;
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
            "UMD: Mapping a hugepage failed. (device: {}, ch: {} errno: {}).",
            physical_device_id,
            index,
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
        return nullptr;
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
            index);
    }

    TracyAllocN(mapping, hugepage_size, "Hugepage");
    return mapping;
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
        auto [noc_address, physical_address] = pci_device_->map_hugepage_to_noc(mapping, actual_size);
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

        if (physical_address == 0) {
            log_warning(
                LogUMD,
                "Failed pinning pages. See logs above. Physical device ID: {}. Channel: {}. See contents of following "
                "files:",
                physical_device_id,
                ch);
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
            "Physical device ID: {}. Channel: {}. Mapping size: {}. Physical address {:#x}",
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

    sysmem_buffer_ = map_sysmem_buffer(iommu_mapping, iommu_mapping_size, true);
    uint64_t iova = sysmem_buffer_->get_iova();
    auto noc_address = sysmem_buffer_->get_noc_address();

    if (!noc_address.has_value()) {
        UMD_THROW(error::RuntimeError, "NOC address is not set for sysmem buffer.");
    }

    if (*noc_address != pcie_base_) {
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

    log_debug(LogUMD, "Mapped sysmem via IOMMU to IOVA {:#x}; NOC address {:#x}", iova, *noc_address);

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

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::pin_and_wrap(
    void *buffer_va,
    size_t buffer_size,
    const bool map_to_noc,
    DeviceBufferAccess device_access,
    SysmemBuffer::Deleter release_backing_memory,
    const bool hugepage) {
    // Pinning covers whole pages, but the buffer sees only the user's range: it gets the IOVA of the user's
    // start, and the deleter keeps the aligned range to unpin.
    const AlignedRange range = page_align(buffer_va, buffer_size);

    uint64_t aligned_iova = 0;
    std::optional<uint64_t> noc_addr = std::nullopt;
    if (hugepage) {
        std::tie(noc_addr, aligned_iova) = pci_device_->map_hugepage_to_noc(range.base, range.mapped_size);
    } else if (map_to_noc) {
        std::tie(noc_addr, aligned_iova) = pci_device_->map_buffer_to_noc(range.base, range.mapped_size, device_access);
    } else {
        aligned_iova = pci_device_->map_for_dma(range.base, range.mapped_size, device_access);
    }

    // Armed immediately after pinning: everything below here can throw, and until create_buffer()
    // returns there is nothing that would unpin the pages.
    UnpinGuard unpin_guard(pci_device_, range.base, range.mapped_size);

    // The buffer's deleter unpins, then releases the backing memory if this manager owns it.
    SysmemBuffer::Deleter deleter = [pci_device = pci_device_,
                                     range,
                                     aligned_iova,
                                     release = std::move(release_backing_memory)](void *) {
        try {
            pci_device->unmap_for_dma(range.base, range.mapped_size);
        } catch (...) {
            log_warning(
                LogUMD, "Failed to unmap sysmem buffer (size: {:#x}, IOVA: {:#x}).", range.mapped_size, aligned_iova);
        }
        if (release) {
            release(range.base);
        }
    };

    // Both device addresses must name the user's start, like the IOVA.
    if (noc_addr.has_value()) {
        *noc_addr += range.offset_from_base;
    }

    // The KMD assigns the NOC address at pin time, so this manager supplies no binder.
    std::unique_ptr<SysmemBuffer> buffer = create_buffer(
        tt_device_,
        buffer_va,
        buffer_size,
        aligned_iova + range.offset_from_base,
        communication_id_,
        std::move(deleter),
        noc_addr,
        device_access);

    // The buffer owns the pin now.
    unpin_guard.disarm();
    return buffer;
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    ZoneScopedC(tracy::Color::Yellow);
    if (!pci_device_->is_iommu_enabled()) {
        return allocate_hugepage_buffer(sysmem_buffer_size);
    }
    void *mapping = mmap_with_hugepage_fallback(sysmem_buffer_size);
    if (mapping == MAP_FAILED) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to allocate sysmem buffer of size {:#x} bytes with mmap.", sysmem_buffer_size));
    }
    // This mapping belongs to the buffer, so it is released along with the pinning. mmap returns a
    // page-aligned address, so the aligned base pin_and_wrap() releases is the one to munmap.
    const size_t mapping_size = sysmem_buffer_size;
    const SysmemBuffer::Deleter release_mapping = [mapping_size](void *aligned_va) {
        if (munmap(aligned_va, mapping_size) != 0) {
            log_warning(
                LogUMD,
                "Failed to munmap sysmem buffer of size {:#x} at {:p}: {}.",
                mapping_size,
                aligned_va,
                strerror(errno));
        }
    };

    try {
        return pin_and_wrap(mapping, sysmem_buffer_size, map_to_noc, DeviceBufferAccess::READ_WRITE, release_mapping);
    } catch (...) {
        // Nothing owns the mmap yet, so free it here rather than leaking it.
        release_mapping(mapping);
        throw;
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_hugepage_buffer(size_t sysmem_buffer_size) {
    UMD_ASSERT(
        sysmem_buffer_size > 0 && sysmem_buffer_size <= HUGEPAGE_REGION_SIZE,
        error::RuntimeError,
        fmt::format(
            "Without an IOMMU a sysmem buffer is one {:#x} byte hugepage, so it cannot hold {:#x} bytes.",
            HUGEPAGE_REGION_SIZE,
            sysmem_buffer_size));
    const std::string hugepage_dir = find_hugepage_dir(HUGEPAGE_REGION_SIZE);
    UMD_ASSERT(
        !hugepage_dir.empty(),
        error::RuntimeError,
        "Without an IOMMU sysmem buffers need 1 GB hugepages, but no hugepage mount was found.");

    // A shared_ptr, as a deleter is a std::function and needs a copyable callable (std::move_only_function is
    // C++23). This copy keeps the page mapped until a failed pin_and_wrap() has unpinned it.
    std::shared_ptr<void> hugepage = acquire_hugepage(hugepage_dir);
    // The KMD binds a hugepage to the NOC as it pins it.
    return pin_and_wrap(
        hugepage.get(),
        sysmem_buffer_size,
        /*map_to_noc=*/true,
        DeviceBufferAccess::READ_WRITE,
        [hugepage](void *) mutable { hugepage.reset(); },
        /*hugepage=*/true);
}

std::shared_ptr<void> SiliconSysmemManager::acquire_hugepage(const std::string &hugepage_dir) {
    const uint16_t index = hugepage_files_->acquire();
    void *mapping = map_hugepage(hugepage_dir, index);
    if (mapping == nullptr) {
        hugepage_files_->release(index);
        UMD_THROW(error::RuntimeError, "Failed to map a hugepage for a sysmem buffer. See the warnings above.");
    }
    // Assumes buffers do not outlive this manager, as their pinning already does for its PCI device.
    return std::shared_ptr<void>(mapping, [files = hugepage_files_.get(), index](void *hugepage) {
        TracyFreeN(hugepage, "Hugepage");
        if (munmap(hugepage, HUGEPAGE_REGION_SIZE) != 0) {
            log_warning(LogUMD, "Failed to munmap hugepage at {:p}: {}.", hugepage, strerror(errno));
        }
        files->release(index);
    });
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    log_debug(LogUMD, "Mapping sysmem buffer to NOC: {:#x}", sysmem_buffer_size);
    // The caller owns this memory, so the buffer only unpins it.
    return pin_and_wrap(buffer, sysmem_buffer_size, map_to_noc, device_access, {});
}

}  // namespace tt::umd
