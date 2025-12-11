// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

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

SysmemManager::SysmemManager(TLBManager *tlb_manager, uint32_t num_host_mem_channels) :
    tlb_manager_(tlb_manager),
    tt_device_(tlb_manager_->get_tt_device()),
    pcie_base_(
        tlb_manager->get_tt_device()->get_arch() == tt::ARCH::WORMHOLE_B0
            ? 0x800000000
            : (tlb_manager->get_tt_device()->get_arch() == tt::ARCH::BLACKHOLE ? 4ULL << 58 : 0)) {
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

bool SysmemManager::pin_or_map_sysmem_to_device() {
    if (tt_device_->get_pci_device()->is_iommu_enabled()) {
        return pin_or_map_iommu();
    } else {
        return pin_or_map_hugepages();
    }
}

SysmemManager::~SysmemManager() { unpin_or_unmap_sysmem(); }

void SysmemManager::unpin_or_unmap_sysmem() {
    // This will unmap the iommu buffer if it was mapped through kmd.
    sysmem_buffer_.reset();
    if (iommu_mapping != nullptr) {
        // This means we have initialized IOMMU mapping, and need to unmap it.
        // It also means that HugepageMappings are faked, so don't unmap them.
        munmap(iommu_mapping, iommu_mapping_size);
        iommu_mapping = nullptr;
    } else {
        for (int ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
            auto &HugepageMapping = hugepage_mapping_per_channel[ch];
            if (HugepageMapping.physical_address &&
                tt_device_->get_pci_device()->is_mapping_buffer_to_noc_supported()) {
                // This will unmap the hugepage if it was mapped through kmd.
                // This is a hack for the 4th hugepage channel which is limited to 768MB.
                size_t actual_size = (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && ch == 3)
                                         ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT
                                         : HugepageMapping.mapping_size;
                tt_device_->get_pci_device()->unmap_for_dma(HugepageMapping.mapping, actual_size);
            }
            if (HugepageMapping.mapping) {
                // Note that we mmap full hugepage, but don't map it filly to NOC.
                // So the hack for 4th hugepage channel is not present in this branch.
                munmap(HugepageMapping.mapping, HugepageMapping.mapping_size);
            }
        }
    }
    hugepage_mapping_per_channel.clear();
}

void SysmemManager::write_to_sysmem(uint16_t channel, const void *src, uint64_t sysmem_dest, uint32_t size) {
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

bool SysmemManager::pin_or_map_hugepages() {
    auto physical_device_id = tt_device_->get_pci_device()->get_device_num();

    bool success = true;

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
        void *mapping = hugepage_mapping_per_channel.at(ch).mapping;
        size_t hugepage_size = hugepage_mapping_per_channel.at(ch).mapping_size;
        size_t actual_size = (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && ch == 3)
                                 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT
                                 : hugepage_size;
        bool map_buffer_to_noc = tt_device_->get_pci_device()->is_mapping_buffer_to_noc_supported();
        uint64_t physical_address, noc_address;
        if (map_buffer_to_noc) {
            std::tie(noc_address, physical_address) =
                tt_device_->get_pci_device()->map_hugepage_to_noc(mapping, actual_size);
            uint64_t expected_noc_address = pcie_base_ + (ch * hugepage_size);

            log_info(LogUMD, "Mapped hugepage {:#x} to NOC address {:#x}", physical_address, noc_address);
            // Note that the truncated page is the final one, so there is no need to
            // give expected_noc_address special treatment for a subsequent page.
            if (noc_address != expected_noc_address) {
                log_warning(
                    LogUMD,
                    "NOC address of a hugepage does not match the expected address. Proceeding could lead to undefined "
                    "behavior");
            }
        } else {
            physical_address = tt_device_->get_pci_device()->map_for_hugepage(mapping, actual_size);
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

bool SysmemManager::init_iommu(uint32_t num_fake_mem_channels) {
    if (num_fake_mem_channels == 0) {
        // No fake hugepage channels requested, so just skip initialization.
        return true;
    }

    constexpr size_t carveout_size = HUGEPAGE_REGION_SIZE - HUGEPAGE_CHANNEL_3_SIZE_LIMIT;  // 1GB - 768MB = 256MB
    const size_t size = num_fake_mem_channels * HUGEPAGE_REGION_SIZE;
    TTDevice *tt_device_ = tlb_manager_->get_tt_device();

    // Caclulate the size of the mapping in order to avoid overlap with PCIE registers on WH.
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && num_fake_mem_channels == 4) {
        iommu_mapping_size = (num_fake_mem_channels == 4) ? (size - carveout_size) : size;
    } else {
        iommu_mapping_size = size;
    }

    log_info(LogUMD, "Initializing iommu for sysmem (size: {:#x}).", iommu_mapping_size);

    if (!tt_device_->get_pci_device()->is_iommu_enabled()) {
        TT_THROW("IOMMU is required for sysmem without hugepages.");
    }

    log_info(LogUMD, "Allocating sysmem without hugepages (size: {:#x}).", iommu_mapping_size);
    iommu_mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    if (iommu_mapping == MAP_FAILED) {
        TT_THROW(
            "UMD: Failed to allocate memory for device/host shared buffer (size: {} errno: {}).",
            size,
            strerror(errno));
    }

    hugepage_mapping_per_channel.resize(num_fake_mem_channels);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (size_t ch = 0; ch < num_fake_mem_channels; ch++) {
        uint8_t *fake_mapping = static_cast<uint8_t *>(iommu_mapping) + ch * HUGEPAGE_REGION_SIZE;
        size_t actual_size = (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0 && ch == 3)
                                 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT
                                 : HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel[ch] = {fake_mapping, actual_size, 0};
    }

    return true;
}

bool SysmemManager::pin_or_map_iommu() {
    if (iommu_mapping == nullptr) {
        // No fake hugepage channels requested, so just skip mapping.
        return true;
    }

    bool map_buffer_to_noc = tt_device_->get_pci_device()->is_mapping_buffer_to_noc_supported();

    sysmem_buffer_ = map_sysmem_buffer(iommu_mapping, iommu_mapping_size, map_buffer_to_noc);
    uint64_t iova = sysmem_buffer_->get_device_io_addr();
    auto noc_address = sysmem_buffer_->get_noc_addr();

    if (map_buffer_to_noc && !noc_address.has_value()) {
        TT_THROW("NOC address is not set for sysmem buffer.");
    }

    if (map_buffer_to_noc && (*noc_address != pcie_base_)) {
        // If this happens, it means that something else is using the address
        // space that UMD typically uses.  Historically, this would have crashed
        // or done something inscrutable.  Now it is just an error.
        log_error(LogUMD, "Expected NOC address: {:#x}, but got {:#x}", pcie_base_, *noc_address);
        TT_THROW("Proceeding could lead to undefined behavior");
    }

    log_info(LogUMD, "Mapped sysmem without hugepages to IOVA {:#x}; NOC address {:#x}", iova, *noc_address);

    for (size_t ch = 0; ch < hugepage_mapping_per_channel.size(); ch++) {
        uint64_t device_io_address = iova + ch * HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel.at(ch).physical_address = device_io_address;
    }

    return true;
}

size_t SysmemManager::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

HugepageMapping SysmemManager::get_hugepage_mapping(size_t channel) const {
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

std::unique_ptr<SysmemBuffer> SysmemManager::allocate_sysmem_buffer(size_t sysmem_buffer_size, const bool map_to_noc) {
    void *mapping =
        mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    return map_sysmem_buffer(mapping, sysmem_buffer_size, map_to_noc);
}

std::unique_ptr<SysmemBuffer> SysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc) {
    log_debug(LogUMD, "Mapping sysmem buffer to NOC: {:#x}", sysmem_buffer_size);
    return std::make_unique<SysmemBuffer>(tlb_manager_, buffer, sysmem_buffer_size, map_to_noc);
}

}  // namespace tt::umd
