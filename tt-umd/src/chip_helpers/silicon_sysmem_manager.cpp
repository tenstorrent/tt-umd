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
// Sets mapping_size to the size actually mapped, which can exceed size when 1GB hugepages are used.
static void *mmap_with_hugepage_fallback(size_t size, size_t &mapping_size) {
    constexpr size_t kHugepage1GiB = 1ULL << 30;
    constexpr size_t kHugepage512MiB = 512ULL << 20;
    constexpr size_t kHugepage2MiB = 2ULL << 20;

    void *addr = MAP_FAILED;
    mapping_size = size;

    // A size of 1GiB or more is rounded up to whole 1GiB hugepages. Host memory channels need this: on WH they stop
    // 256MB short of a whole number of gigabytes, and smaller pages would multiply their IOMMU entries.
    const size_t size_in_1gib_pages = (size + kHugepage1GiB - 1) / kHugepage1GiB * kHugepage1GiB;
    if (size >= kHugepage1GiB) {
        addr = mmap(
            nullptr,
            size_in_1gib_pages,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB | MAP_POPULATE,
            -1,
            0);
        if (addr != MAP_FAILED) {
            log_debug(LogUMD, "Allocated {:#x} bytes using 1GB hugepages.", size_in_1gib_pages);
            mapping_size = size_in_1gib_pages;
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

SiliconSysmemManager::SiliconSysmemManager(TTDevice *tt_device) : hugepage_files_(std::make_shared<HugepageFiles>()) {
    tt_device_ = tt_device;
    pci_device_ = tt_device->get_pci_device();
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "PCI device not available in TTDevice.");
    communication_id_ = tt_device->get_communication_device_id();
    pcie_base_ = get_pcie_base_for_arch(pci_device_->get_arch());
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
    size_t mapping_size = 0;
    void *mapping = mmap_with_hugepage_fallback(sysmem_buffer_size, mapping_size);
    if (mapping == MAP_FAILED) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to allocate sysmem buffer of size {:#x} bytes with mmap.", sysmem_buffer_size));
    }
    // This mapping belongs to the buffer, so it is released along with the pinning. mmap returns a
    // page-aligned address, so the aligned base pin_and_wrap() releases is the one to munmap.
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

    const uint16_t index = hugepage_files_->acquire();
    void *mapping = map_hugepage(hugepage_dir, index);
    if (mapping == nullptr) {
        hugepage_files_->release(index);
        UMD_THROW(error::RuntimeError, "Failed to map a hugepage for a sysmem buffer. See the warnings above.");
    }
    // The hugepage and its file belong to the buffer, so both are released along with the pinning. The weak
    // pointer lets a buffer outlive this manager.
    const SysmemBuffer::Deleter release_hugepage = [weak_files = std::weak_ptr<HugepageFiles>(hugepage_files_),
                                                    index](void *hugepage) {
        TracyFreeN(hugepage, "Hugepage");
        if (munmap(hugepage, HUGEPAGE_REGION_SIZE) != 0) {
            log_warning(LogUMD, "Failed to munmap hugepage at {:p}: {}.", hugepage, strerror(errno));
        }
        if (auto files = weak_files.lock()) {
            files->release(index);
        }
    };

    try {
        // The KMD binds a hugepage to the NOC as it pins it.
        return pin_and_wrap(
            mapping,
            sysmem_buffer_size,
            /*map_to_noc=*/true,
            DeviceBufferAccess::READ_WRITE,
            release_hugepage,
            /*hugepage=*/true);
    } catch (...) {
        // Nothing owns the hugepage yet, so free it here rather than leaking it.
        release_hugepage(mapping);
        throw;
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    log_debug(LogUMD, "Mapping sysmem buffer to NOC: {:#x}", sysmem_buffer_size);
    // The caller owns this memory, so the buffer only unpins it.
    return pin_and_wrap(buffer, sysmem_buffer_size, map_to_noc, device_access, {});
}

}  // namespace tt::umd
