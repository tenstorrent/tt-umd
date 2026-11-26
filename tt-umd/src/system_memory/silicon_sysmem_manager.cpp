/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tt-umd/system_memory/silicon_sysmem_manager.hpp"

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

#include "cpuset_lib.hpp"
#include "hugepage.hpp"
#include "system_memory/pcie_base.hpp"
#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/system_memory/sysmem_buffer.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_types.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

// Try hugepage sizes from largest to smallest, then fall back to regular pages.
// Larger hugepages are strictly preferred: each hugepage maps to one SMMU TLB entry
// regardless of size, so 1GB (PUD/level-1 block) consumes half as many TLB entries as
// 512MB (PMD/level-2 block) for the same total memory, reducing IOMMU mapping overhead.
// 512MB is only meaningful on AArch64 with 64K base pages; it is not exposed on x86.
// Sets mapping_size to the size actually mapped, which exceeds size when it is rounded up to whole hugepages.
static void *mmap_with_hugepage_fallback(size_t size, size_t &mapping_size) {
    constexpr size_t kHugepage1GiB = 1ULL << 30;
    constexpr size_t kHugepage512MiB = 512ULL << 20;
    constexpr size_t kHugepage2MiB = 2ULL << 20;

    void *addr = MAP_FAILED;
    mapping_size = size;

    // A size of at least one 1GiB or 512MiB hugepage is rounded up to whole hugepages of that size. Host memory
    // channels need this: on WH they stop 256MB short of a whole number of gigabytes, and smaller pages would
    // multiply their IOMMU entries.
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
    const size_t size_in_512mib_pages = (size + kHugepage512MiB - 1) / kHugepage512MiB * kHugepage512MiB;
    if (size >= kHugepage512MiB) {
        addr = mmap(
            nullptr,
            size_in_512mib_pages,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_512MB | MAP_POPULATE,
            -1,
            0);
        if (addr != MAP_FAILED) {
            log_debug(LogUMD, "Allocated {:#x} bytes using 512MB hugepages.", size_in_512mib_pages);
            mapping_size = size_in_512mib_pages;
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

namespace {

// The page-aligned extent covering a buffer. Pinning works on whole pages, so an unaligned start is
// rounded down and the size grown to cover the user's whole range.
struct AlignedRange {
    void *base = nullptr;           // What gets pinned and released.
    size_t mapped_size = 0;         // Page-rounded size covering the user's whole range.
    uint64_t offset_from_base = 0;  // Distance from base to the user's virtual address.
};

AlignedRange page_align(void *buffer_va, size_t buffer_size) {
    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t va = reinterpret_cast<uint64_t>(buffer_va);
    const uint64_t base = va & ~(page_size - 1);
    AlignedRange range{};
    range.base = reinterpret_cast<void *>(base);
    range.offset_from_base = va - base;
    range.mapped_size = (buffer_size + range.offset_from_base + page_size - 1) & ~(page_size - 1);
    return range;
}

}  // namespace

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

SiliconSysmemManager::SiliconSysmemManager(PCIDevice *pci_device, int communication_id) :
    pci_device_(pci_device), communication_id_(communication_id) {
    UMD_ASSERT(pci_device != nullptr, error::RuntimeError, "Silicon sysmem manager needs a PCI device.");
    pcie_base_ = get_pcie_base_for_arch(pci_device_->get_arch());
}

bool SiliconSysmemManager::is_read_only_page_pinning_supported() const {
    return pci_device_->is_read_only_page_pinning_supported();
}

// Without an IOMMU the device sees physical addresses, so scattered user pages cannot be presented as one buffer.
bool SiliconSysmemManager::is_user_buffer_mapping_supported() const { return pci_device_->is_iommu_enabled(); }

std::optional<size_t> SiliconSysmemManager::get_max_buffer_count(size_t size) const {
    // With an IOMMU buffers are ordinary memory, so any number fits.
    if (pci_device_->is_iommu_enabled()) {
        return std::nullopt;
    }
    // Without one every buffer is a 1 GB hugepage, and the host's hugepages are shared between its devices.
    if (size > HUGEPAGE_REGION_SIZE) {
        return 0;
    }
    const uint32_t hugepages_per_device =
        get_num_hugepages_per_device(pci_device_->get_pci_device_id(), pci_device_->get_pci_revision());
    log_debug(
        LogUMD,
        "Using {} Hugepages/NumHostMemChannels for PCIDevice {}",
        hugepages_per_device,
        pci_device_->get_device_num());
    if (find_hugepage_dir(HUGEPAGE_REGION_SIZE).empty()) {
        log_warning(LogUMD, "No huge page mount found for hugepage_size: {}.", HUGEPAGE_REGION_SIZE);
        return 0;
    }
    return hugepages_per_device;
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
    // Pinning covers whole pages, but the buffer sees only the user's range: it gets the device addresses of
    // the user's first byte, and the deleter keeps the aligned range to unpin.
    const AlignedRange range = page_align(buffer_va, buffer_size);

    uint64_t aligned_iova = 0;
    std::optional<uint64_t> noc_addr = std::nullopt;
    if (map_to_noc) {
        std::tie(noc_addr, aligned_iova) =
            hugepage ? pci_device_->map_hugepage_to_noc(range.base, range.mapped_size)
                     : pci_device_->map_buffer_to_noc(range.base, range.mapped_size, device_access);
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

    // The KMD maps the NOC window linearly onto the IOVA, so the user's offset into its first page
    // applies to both device addresses.
    const uint64_t iova = aligned_iova + range.offset_from_base;
    if (noc_addr.has_value()) {
        *noc_addr += range.offset_from_base;
    }

    // The KMD assigns the NOC address at pin time, so this manager supplies no binder.
    std::unique_ptr<SysmemBuffer> buffer =
        create_buffer(buffer_va, buffer_size, iova, communication_id_, std::move(deleter), noc_addr, device_access);

    // The buffer owns the pin now.
    unpin_guard.disarm();
    return buffer;
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_buffer(size_t size, bool bind_to_noc) {
    ZoneScopedC(tracy::Color::Yellow);
    const bool hugepage = !pci_device_->is_iommu_enabled();
    if (hugepage) {
        UMD_ASSERT(
            size > 0 && size <= HUGEPAGE_REGION_SIZE,
            error::RuntimeError,
            fmt::format(
                "Without an IOMMU a sysmem buffer is one {:#x} byte hugepage, so it cannot hold {:#x} bytes.",
                HUGEPAGE_REGION_SIZE,
                size));
    }
    // The size actually mapped: one whole hugepage without an IOMMU, else what the fallback rounded the request up to.
    size_t mapping_size = HUGEPAGE_REGION_SIZE;
    // Without an IOMMU the device reaches only physically contiguous memory, so a buffer is one whole 1 GB
    // hugepage. It is anonymous, so unlike the channels' hugetlbfs files no other allocator or process can map
    // it, and MAP_SHARED like them, so a fork() cannot copy-on-write this process away from the pinned page.
    void *mapping = hugepage ? mmap(
                                   nullptr,
                                   HUGEPAGE_REGION_SIZE,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB | MAP_POPULATE,
                                   -1,
                                   0)
                             : mmap_with_hugepage_fallback(size, mapping_size);
    if (mapping == MAP_FAILED) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to allocate sysmem buffer of size {:#x} bytes with mmap: {}.{}",
                size,
                strerror(errno),
                hugepage ? " Without an IOMMU each sysmem buffer needs a free 1 GB hugepage." : ""));
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
        return pin_and_wrap(mapping, size, bind_to_noc, DeviceBufferAccess::READ_WRITE, release_mapping, hugepage);
    } catch (...) {
        // Nothing owns the mmap yet, so free it here rather than leaking it.
        release_mapping(mapping);
        throw;
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_channel_buffer(uint32_t channel, size_t size) {
    ZoneScopedC(tracy::Color::Yellow);
    if (pci_device_->is_iommu_enabled()) {
        return allocate_buffer(size, /*bind_to_noc=*/true);
    }
    UMD_ASSERT(
        size > 0 && size <= HUGEPAGE_REGION_SIZE,
        error::RuntimeError,
        fmt::format(
            "Without an IOMMU a host memory channel is one {:#x} byte hugepage, so it cannot hold {:#x} bytes.",
            HUGEPAGE_REGION_SIZE,
            size));
    const int device_num = pci_device_->get_device_num();
    const std::string hugepage_dir = find_hugepage_dir(HUGEPAGE_REGION_SIZE);
    UMD_ASSERT(
        !hugepage_dir.empty(),
        error::RuntimeError,
        fmt::format("No hugetlbfs mount found for {:#x} byte hugepages.", HUGEPAGE_REGION_SIZE));

    // The file is named by device and channel, so the channel is the same hugepage across restarts and for every
    // process using the device.
    const int hugepage_fd = open_hugepage_file(hugepage_dir, device_num, channel);
    UMD_ASSERT(
        hugepage_fd != -1,
        error::RuntimeError,
        fmt::format("Creating the hugepage file of device {} channel {} failed.", device_num, channel));

    struct stat hugepage_st {};

    if (fstat(hugepage_fd, &hugepage_st) == -1) {
        log_warning(LogUMD, "Error reading hugepage file size after opening.");
    }
    void *mapping =
        mmap(nullptr, HUGEPAGE_REGION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, hugepage_fd, 0);
    const int mmap_errno = errno;
    close(hugepage_fd);
    if (mapping == MAP_FAILED) {
        if (hugepage_st.st_size == 0) {
            log_warning(
                LogUMD,
                "Opened hugepage file has zero size, mapping might've failed due to that. Verify that enough "
                "hugepages are provided.");
        }
        print_file_contents("/proc/cmdline");
        print_file_contents("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages");  // Hardcoded for 1GB.
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Mapping the hugepage of device {} channel {} failed: {}.", device_num, channel, strerror(mmap_errno)));
    }

    // Device-to-host transfers are faster when the hugepage sits on the device's NUMA node.
    if (!tt::cpuset::cpuset_allocator::bind_area_to_memory_nodeset(device_num, mapping, HUGEPAGE_REGION_SIZE)) {
        log_warning(
            LogUMD,
            "bind_area_to_memory_nodeset() failed (device: {} channel: {}). Hugepage allocation is not on the NUMA "
            "node matching the TT device. Side-Effect is decreased Device->Host perf (Issue #893).",
            device_num,
            channel);
    }

    const SysmemBuffer::Deleter release_mapping = [](void *aligned_va) {
        if (munmap(aligned_va, HUGEPAGE_REGION_SIZE) != 0) {
            log_warning(
                LogUMD, "Failed to munmap host memory channel hugepage at {:p}: {}.", aligned_va, strerror(errno));
        }
    };

    try {
        return pin_and_wrap(
            mapping, size, /*map_to_noc=*/true, DeviceBufferAccess::READ_WRITE, release_mapping, /*hugepage=*/true);
    } catch (...) {
        log_warning(
            LogUMD,
            "Failed pinning the hugepage of device {} channel {}. See contents of the following files:",
            device_num,
            channel);
        print_file_contents("/sys/module/tenstorrent/version", "(TTKMD version)");
        print_file_contents("/proc/meminfo");
        print_file_contents("/proc/buddyinfo");
        // Nothing owns the mmap yet, so free it here rather than leaking it.
        release_mapping(mapping);
        throw;
    }
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_user_buffer(
    void *user_ptr, size_t size, bool bind_to_noc, DeviceBufferAccess device_access) {
    // Refuse here: without an IOMMU the pin alone still accepts physically contiguous memory.
    UMD_ASSERT(
        is_user_buffer_mapping_supported(),
        error::RuntimeError,
        "This allocator cannot map user buffers; check is_user_buffer_mapping_supported() first.");
    log_debug(LogUMD, "Mapping sysmem buffer to NOC: {:#x}", size);
    // The caller owns this memory, so the buffer only unpins it.
    return pin_and_wrap(user_ptr, size, bind_to_noc, device_access, {});
}

}  // namespace tt::umd
