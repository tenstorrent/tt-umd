/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/pcie/pci_device.hpp"

#include <fcntl.h>      // for ::open
#include <linux/pci.h>  // for PCI_SLOT, PCI_FUNC
#include <sys/ioctl.h>  // for ioctl
#include <sys/mman.h>   // for mmap, munmap
#include <unistd.h>     // for ::close

#include <cstdint>
#include <cstring>  // for memcpy
#include <filesystem>
#include <fstream>
#include <optional>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "ioctl.h"
#include "umd/device/tt_kmd_lib/tt_kmd_lib.h"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/kmd_versions.hpp"
#include "utils.hpp"

namespace tt::umd {

static const uint16_t GS_PCIE_DEVICE_ID = 0xfaca;
static const uint16_t WH_PCIE_DEVICE_ID = 0x401e;
static const uint16_t BH_PCIE_DEVICE_ID = 0xb140;

// TODO: we'll have to rethink this when KMD takes control of the inbound PCIe
// TLB windows and there is no longer a pre-defined WC/UC split.
static const uint32_t GS_BAR0_WC_MAPPING_SIZE = (156 << 20) + (10 << 21) + (18 << 24);

// Defines the address for WC region. addresses 0 to BH_BAR0_WC_MAPPING_SIZE are in WC, above that are UC
static const uint32_t BH_BAR0_WC_MAPPING_SIZE = 188 << 21;

template <typename T>
static std::optional<T> try_read_sysfs(const PciDeviceInfo &device_info, const std::string &attribute_name) {
    const auto sysfs_path = fmt::format(
        "/sys/bus/pci/devices/{:04x}:{:02x}:{:02x}.{:x}/{}",
        device_info.pci_domain,
        device_info.pci_bus,
        device_info.pci_device,
        device_info.pci_function,
        attribute_name);
    std::ifstream attribute_file(sysfs_path);
    std::string value_str;
    T value;

    if (!attribute_file.is_open() || !std::getline(attribute_file, value_str)) {
        return std::nullopt;
    }

    std::istringstream iss(value_str);

    // Handle hexadecimal input for integer types
    if constexpr (std::is_integral_v<T>) {
        if (value_str.substr(0, 2) == "0x") {
            iss >> std::hex;
        }
    }

    if (!(iss >> value)) {
        return std::nullopt;
    }

    return value;
}

template <typename T>
static T read_sysfs(const PciDeviceInfo &device_info, const std::string &attribute_name) {
    auto result = try_read_sysfs<T>(device_info, attribute_name);
    if (!result) {
        const auto sysfs_path = fmt::format(
            "/sys/bus/pci/devices/{:04x}:{:02x}:{:02x}.{:x}/{}",
            device_info.pci_domain,
            device_info.pci_bus,
            device_info.pci_device,
            device_info.pci_function,
            attribute_name);
        TT_THROW("Failed reading or parsing sysfs attribute: {}", sysfs_path);
    }
    return *result;
}

template <typename T>
T read_sysfs(const PciDeviceInfo &device_info, const std::string &attribute_name, const T &default_value) {
    auto result = try_read_sysfs<T>(device_info, attribute_name);
    return result.value_or(default_value);
}

static bool detect_iommu(const PciDeviceInfo &device_info) {
    auto iommu_type = try_read_sysfs<std::string>(device_info, "iommu_group/type");
    if (iommu_type) {
        return iommu_type->substr(0, 3) == "DMA";  // DMA or DMA-FQ
    }
    return false;
}

static std::optional<uint8_t> try_read_config_byte(const PciDeviceInfo &device_info, size_t offset) {
    const auto config_path = fmt::format("/sys/bus/pci/devices/{}/config", device_info.pci_bdf);

    std::ifstream config_file(config_path, std::ios::binary);
    if (!config_file.is_open()) {
        return std::nullopt;
    }

    config_file.seekg(offset);
    uint8_t byte;
    if (!config_file.read(reinterpret_cast<char *>(&byte), 1)) {
        return std::nullopt;
    }

    return byte;
}

static std::string get_pci_bdf(
    const uint16_t pci_domain, const uint16_t pci_bus, const uint16_t pci_device, const uint16_t pci_function) {
    return fmt::format("{:04x}:{:02x}:{:02x}.{:x}", pci_domain, pci_bus, pci_device, pci_function);
}

static bool is_number(const std::string &str) { return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit); }

static std::optional<int> get_physical_slot_for_pcie_bdf(const std::string &target_bdf) {
    std::string base_path = "/sys/bus/pci/slots";

    for (const auto &entry : std::filesystem::directory_iterator(base_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        std::string dir_name = entry.path().filename().string();
        if (!is_number(dir_name)) {
            continue;
        }

        int slot_number = std::stoi(dir_name);
        std::string address_file_path = entry.path().string() + "/address";

        if (!std::filesystem::exists(address_file_path)) {
            continue;
        }

        std::ifstream address_file(address_file_path);
        if (!address_file.is_open()) {
            continue;
        }

        std::string bdf;
        if (!std::getline(address_file, bdf)) {
            continue;
        }

        bdf.erase(bdf.find_last_not_of(" \n\r\t") + 1);

        // Append the pci_function 0, as our PCI devices are single function.
        bdf += ".0";

        if (bdf == target_bdf) {
            return slot_number;
        }
    }

    return std::nullopt;
}

static PciDeviceInfo read_device_info(int fd) {
    tenstorrent_get_device_info info{};
    info.in.output_size_bytes = sizeof(info.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) < 0) {
        TT_THROW("TENSTORRENT_IOCTL_GET_DEVICE_INFO failed");
    }

    uint16_t bus = info.out.bus_dev_fn >> 8;
    uint16_t dev = (info.out.bus_dev_fn >> 3) & 0x1F;
    uint16_t fn = info.out.bus_dev_fn & 0x07;

    std::string pci_bdf = get_pci_bdf(info.out.pci_domain, bus, dev, fn);

    return PciDeviceInfo{
        info.out.vendor_id,
        info.out.device_id,
        info.out.pci_domain,
        bus,
        dev,
        fn,
        pci_bdf,
        get_physical_slot_for_pcie_bdf(pci_bdf)};
}

static void reset_device_ioctl(std::unordered_set<int> pci_target_devices, uint32_t flags) {
    for (int n : PCIDevice::enumerate_devices(pci_target_devices)) {
        int fd = open(fmt::format("/dev/tenstorrent/{}", n).c_str(), O_RDWR | O_CLOEXEC);
        if (fd == -1) {
            continue;
        }

        try {
            tenstorrent_reset_device reset_info{};

            reset_info.in.output_size_bytes = sizeof(reset_info.out);
            reset_info.in.flags = flags;

            reset_info.out.output_size_bytes = 0;
            reset_info.out.result = 0;
            if (ioctl(fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset_info) == -1) {
                TT_THROW("TENSTORRENT_IOCTL_RESET_DEVICE failed");
            }
        } catch (...) {
        }

        close(fd);
    }
}

tt::ARCH PciDeviceInfo::get_arch() const {
    if (this->device_id == WH_PCIE_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (this->device_id == BH_PCIE_DEVICE_ID) {
        return tt::ARCH::BLACKHOLE;
    }
    return tt::ARCH::Invalid;
}

std::vector<int> PCIDevice::enumerate_devices(std::unordered_set<int> pci_target_devices) {
    std::vector<int> device_ids;
    std::string path = "/dev/tenstorrent/";

    if (!std::filesystem::exists(path)) {
        return device_ids;
    }

    std::unordered_set<int> visible_devices = tt::umd::utils::get_visible_devices(pci_target_devices);

    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        std::string filename = entry.path().filename().string();

        // TODO: this will skip any device that has a non-numeric name, which
        // is probably what we want longer-term (i.e. a UUID or something).
        if (std::all_of(filename.begin(), filename.end(), ::isdigit)) {
            int pci_device_id = std::stoi(filename);
            if (visible_devices.empty() || visible_devices.find(pci_device_id) != visible_devices.end()) {
                device_ids.push_back(pci_device_id);
            }
        }
    }

    std::sort(device_ids.begin(), device_ids.end());
    return device_ids;
}

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info(std::unordered_set<int> pci_target_devices) {
    std::map<int, PciDeviceInfo> infos;
    for (int n : PCIDevice::enumerate_devices(pci_target_devices)) {
        int fd = open(fmt::format("/dev/tenstorrent/{}", n).c_str(), O_RDWR | O_CLOEXEC);
        if (fd == -1) {
            continue;
        }

        try {
            infos[n] = read_device_info(fd);
        } catch (...) {
        }

        close(fd);
    }
    return infos;
}

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(fmt::format("/dev/tenstorrent/{}", pci_device_number)),
    pci_device_num(pci_device_number),
    pci_device_file_desc(open(device_path.c_str(), O_RDWR | O_CLOEXEC)),
    info(read_device_info(pci_device_file_desc)),
    numa_node(read_sysfs<int>(info, "numa_node", -1)),  // default to -1 if not found
    revision(read_sysfs<int>(info, "revision")),
    arch(info.get_arch()),
    kmd_version(PCIDevice::read_kmd_version()),
    iommu_enabled(detect_iommu(info)) {
    if (iommu_enabled && kmd_version < KMD_IOMMU) {
        TT_THROW("Running with IOMMU support requires KMD version {} or newer", KMD_IOMMU.to_string());
    }
    if (kmd_version < KMD_TLBS) {
        TT_THROW("Running UMD requires KMD version {} or newer.", KMD_TLBS.to_string());
    }

    if (iommu_enabled && kmd_version < KMD_MAP_TO_NOC) {
        log_warning(
            LogUMD,
            "Running with IOMMU support prior to KMD version {} is of limited support.",
            KMD_MAP_TO_NOC.to_string());
    }

    tt_device_open(device_path.c_str(), &tt_device_handle);

    tenstorrent_get_driver_info driver_info{};
    driver_info.in.output_size_bytes = sizeof(driver_info.out);
    if (ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &driver_info) == -1) {
        TT_THROW("TENSTORRENT_IOCTL_GET_DRIVER_INFO failed");
    }

    log_debug(
        LogUMD,
        "Opened PCI device {}; KMD version: {}; API: {}; IOMMU: {}",
        pci_device_num,
        kmd_version.to_string(),
        driver_info.out.driver_version,
        iommu_enabled ? "enabled" : "disabled");

    TT_ASSERT(arch != tt::ARCH::WORMHOLE_B0 || revision == 0x01, "Wormhole B0 must have revision 0x01");

    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;

    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(fmt::format("Query mappings failed on device {}.", pci_device_num));
    }

    // Mapping resource to BAR
    // Resource 0 -> BAR0
    // Resource 1 -> BAR2
    // Resource 2 -> BAR4
    tenstorrent_mapping bar0_uc_mapping{};
    tenstorrent_mapping bar0_wc_mapping{};
    tenstorrent_mapping bar2_uc_mapping{};
    tenstorrent_mapping bar2_wc_mapping{};
    tenstorrent_mapping bar4_uc_mapping{};
    tenstorrent_mapping bar4_wc_mapping{};

    for (unsigned int i = 0; i < mappings.query_mappings.in.output_mapping_count; i++) {
        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_UC) {
            bar0_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
            bar0_wc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE1_UC) {
            bar2_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE1_WC) {
            bar2_wc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_UC) {
            bar4_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_WC) {
            bar4_wc_mapping = mappings.mapping_array[i];
        }

        log_debug(
            LogUMD,
            "BAR mapping id {} base {} size {}",
            mappings.mapping_array[i].mapping_id,
            reinterpret_cast<void *>(mappings.mapping_array[i].mapping_base),
            mappings.mapping_array[i].mapping_size);
    }

    if (bar0_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE0_UC) {
        throw std::runtime_error(fmt::format("Device {} has no BAR0 UC mapping.", pci_device_num));
    }

    bar0 = mmap(
        NULL,
        PCIDevice::bar0_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        pci_device_file_desc,
        bar0_uc_mapping.mapping_base + PCIDevice::bar0_mapping_offset);

    if (bar0 == MAP_FAILED) {
        throw std::runtime_error(fmt::format("BAR0 mapping failed for device {}.", pci_device_num));
    }

    if (arch == tt::ARCH::WORMHOLE_B0) {
        if (bar4_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 UC mapping.", pci_device_num));
        }

        bar2_uc_size = bar2_uc_mapping.mapping_size;
        bar2_uc = mmap(
            NULL,
            bar2_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            pci_device_file_desc,
            bar2_uc_mapping.mapping_base);

        if (bar2_uc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR2 UC mapping failed for device {}.", pci_device_num));
        }
    } else if (arch == tt::ARCH::BLACKHOLE) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE1_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR2 UC mapping.", pci_device_num));
        }

        // Using UnCachable memory mode. This is used for accessing registers on Blackhole.
        bar2_uc_size = bar2_uc_mapping.mapping_size;
        bar2_uc = mmap(
            NULL,
            bar2_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            pci_device_file_desc,
            bar2_uc_mapping.mapping_base);

        if (bar2_uc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR2 UC mapping failed for device {}.", pci_device_num));
        }

        if (bar4_wc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_WC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 WC mapping.", pci_device_num));
        }

        // Using Write-Combine memory mode. This is used for accessing DRAM on Blackhole.
        // WC doesn't guarantee write ordering but has better performance.
        bar4_wc_size = bar4_wc_mapping.mapping_size;
        bar4_wc = mmap(
            NULL,
            bar4_wc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            pci_device_file_desc,
            bar4_wc_mapping.mapping_base);

        if (bar4_wc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR4 WC mapping failed for device {}.", pci_device_num));
        }
    }

    allocate_pcie_dma_buffer();
}

PCIDevice::~PCIDevice() {
    tt_device_close(tt_device_handle);
    close(pci_device_file_desc);

    if (bar0 != nullptr && bar0 != MAP_FAILED) {
        munmap(bar0, bar0_size);
    }

    if (bar2_uc != nullptr && bar2_uc != MAP_FAILED) {
        munmap(bar2_uc, bar2_uc_size);
    }

    if (bar4_wc != nullptr && bar4_wc != MAP_FAILED) {
        munmap(bar4_wc, bar4_wc_size);
    }

    if (dma_buffer.buffer != nullptr && dma_buffer.buffer != MAP_FAILED) {
        munmap(dma_buffer.buffer, dma_buffer.size + 0x1000);
    }
}

uint64_t PCIDevice::map_for_hugepage(void *buffer, size_t size) {
    tenstorrent_pin_pages pin_pages;
    memset(&pin_pages, 0, sizeof(pin_pages));
    pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
    pin_pages.in.flags = TENSTORRENT_PIN_PAGES_CONTIGUOUS;
    pin_pages.in.virtual_address = reinterpret_cast<std::uintptr_t>(buffer);
    pin_pages.in.size = size;

    if (ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) == -1) {
        log_warning(
            LogUMD,
            "Failed to pin pages for hugepage at virtual address {} with size {} and flags {}: {}",
            fmt::format("{:#x}", pin_pages.in.virtual_address),
            fmt::format("{:#x}", pin_pages.in.size),
            fmt::format("{:#x}", pin_pages.in.flags),
            strerror(errno));
        return 0;
    }

    log_debug(
        LogUMD,
        "Pinning pages for Hugepage: virtual address {:#x} and size {:#x} pinned to physical address {:#x}",
        pin_pages.in.virtual_address,
        pin_pages.in.size,
        pin_pages.out.physical_address);

    return pin_pages.out.physical_address;
}

bool PCIDevice::is_mapping_buffer_to_noc_supported() { return PCIDevice::read_kmd_version() >= KMD_MAP_TO_NOC; }

std::pair<uint64_t, uint64_t> PCIDevice::map_buffer_to_noc(void *buffer, size_t size) {
    if (PCIDevice::read_kmd_version() < KMD_MAP_TO_NOC) {
        TT_THROW("KMD version must be at least 2.0.0 to use buffer with NOC mapping");
    }

    tt_dma_t *dma_handle;
    const uint32_t flags = tt_dma_map_flags::TT_DMA_FLAG_NOC;
    tt_dma_map(tt_device_handle, buffer, size, flags, &dma_handle);

    uint64_t noc_addr;
    tt_dma_get_noc_addr(dma_handle, &noc_addr);

    uint64_t iova;
    tt_dma_get_dma_addr(dma_handle, &iova);

    // log_debug(
    //     LogUMD,
    //     "Pinning pages for DMA: virtual address {:#x} and size {:#x} pinned to physical address {:#x} and mapped to "
    //     "noc address {:#x}",
    //     pin.in.virtual_address,
    //     pin.in.size,
    //     pin.out.physical_address,
    //     pin.out.noc_address);

    return {noc_addr, iova};
}

std::pair<uint64_t, uint64_t> PCIDevice::map_hugepage_to_noc(void *hugepage, size_t size) {
    if (PCIDevice::read_kmd_version() < KMD_MAP_TO_NOC) {
        TT_THROW("KMD version must be at least 2.0.0 to use buffer with NOC mapping");
    }

    tt_dma_t *dma_handle;
    const uint32_t flags = tt_dma_map_flags::TT_DMA_FLAG_CONTIGUOUS | tt_dma_map_flags::TT_DMA_FLAG_NOC;

    tt_dma_map(tt_device_handle, hugepage, size, flags, &dma_handle);

    uint64_t noc_addr;
    tt_dma_get_noc_addr(dma_handle, &noc_addr);

    uint64_t iova;
    tt_dma_get_dma_addr(dma_handle, &iova);

    //    log_debug(
    //         LogUMD,
    //         "Pinning pages for Hugepage: virtual address {:#x} and size {:#x} pinned to physical address {:#x} and
    //         mapped " "to noc address {:#x}", pin.in.virtual_address, pin.in.size, pin.out.physical_address,
    //         pin.out.noc_address);

    return {noc_addr, iova};
}

uint64_t PCIDevice::map_for_dma(void *buffer, size_t size) {
    tt_dma_t *dma_handle;

    const uint32_t flags = is_iommu_enabled() ? 0 : TENSTORRENT_PIN_PAGES_CONTIGUOUS;
    tt_dma_map(tt_device_handle, buffer, size, flags, &dma_handle);

    uint64_t iova;
    tt_dma_get_dma_addr(dma_handle, &iova);

    //  log_debug(
    //     LogUMD,
    //     "Pinning pages for DMA: virtual address {:#x} and size {:#x} pinned to physical address {:#x} without mapping
    //     " "to noc", pin_pages.in.virtual_address, pin_pages.in.size, pin_pages.out.physical_address);

    return iova;
}

void PCIDevice::unmap_for_dma(void *buffer, size_t size) {
    static const auto page_size = sysconf(_SC_PAGESIZE);

    const uint64_t vaddr = reinterpret_cast<uint64_t>(buffer);

    if (vaddr % page_size != 0 || size % page_size != 0) {
        TT_THROW("Buffer must be page-aligned with a size that is a multiple of the page size");
    }

    tenstorrent_unpin_pages unpin_pages{};
    unpin_pages.in.virtual_address = vaddr;
    unpin_pages.in.size = size;

    if (ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin_pages) < 0) {
        TT_THROW(
            "Failed to unpin pages for DMA buffer at virtual address {} and size {}: {}",
            fmt::format("{:#x}", vaddr),
            fmt::format("{:#x}", size),
            strerror(errno));
    }

    log_debug(
        LogUMD,
        "Unpinning pages for DMA: virtual address {:#x} and size {:#x}",
        unpin_pages.in.virtual_address,
        unpin_pages.in.size);
}

semver_t PCIDevice::read_kmd_version() {
    static const std::string path = "/sys/module/tenstorrent/version";
    std::ifstream file(path);

    if (!file.is_open()) {
        log_warning(LogUMD, "Failed to open file: {}", path);
        return {0, 0, 0};
    }

    std::string version_str;
    std::getline(file, version_str);

    return semver_t(version_str);
}

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(const size_t tlb_size, const TlbMapping tlb_mapping) {
    return std::make_unique<TlbHandle>(tt_device_handle, tlb_size, tlb_mapping);
}

void PCIDevice::reset_device_ioctl(std::unordered_set<int> pci_target_devices, TenstorrentResetDevice flag) {
    umd::reset_device_ioctl(pci_target_devices, static_cast<uint32_t>(flag));
}

uint8_t PCIDevice::read_command_byte(const int pci_device_num) {
    int fd = open(fmt::format("/dev/tenstorrent/{}", pci_device_num).c_str(), O_RDWR | O_CLOEXEC);
    if (fd == -1) {
        TT_THROW("Coudln't open file descriptor for PCI device number: {}", pci_device_num);
    }
    auto device_info = read_device_info(fd);

    auto command_byte = try_read_config_byte(device_info, 4);
    if (!command_byte) {
        const auto sysfs_path = fmt::format(
            "/sys/bus/pci/devices/{:04x}:{:02x}:{:02x}.{:x}/{}",
            device_info.pci_domain,
            device_info.pci_bus,
            device_info.pci_device,
            device_info.pci_function);
        TT_THROW("Failed reading or parsing sysfs config: {}", sysfs_path);
    }
    return *command_byte;
}

bool PCIDevice::try_allocate_pcie_dma_buffer_iommu(const size_t dma_buf_size) {
    const size_t dma_buf_alloc_size = dma_buf_size + 0x1000;  // + 0x1000 for completion page

    void *dma_buf_mapping =
        mmap(nullptr, dma_buf_alloc_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    try {
        uint64_t iova = map_for_dma(dma_buf_mapping, dma_buf_alloc_size);

        dma_buffer.buffer = static_cast<uint8_t *>(dma_buf_mapping);
        dma_buffer.completion = static_cast<uint8_t *>(dma_buf_mapping) + dma_buf_size;
        dma_buffer.buffer_pa = iova;
        dma_buffer.completion_pa = iova + dma_buf_size;
        dma_buffer.size = dma_buf_size;

        return true;
    } catch (...) {
        log_debug(LogUMD, "Failed to allocate PCIe DMA buffer of size {} with IOMMU enabled.", dma_buf_size);
        munmap(dma_buf_mapping, dma_buf_alloc_size);
        return false;
    }
}

bool PCIDevice::try_allocate_pcie_dma_buffer_no_iommu(const size_t dma_buf_size) {
    tenstorrent_allocate_dma_buf dma_buf{};

    const uint64_t dma_buf_alloc_size = dma_buf_size + 0x1000;  // + 0x1000 for completion page

    dma_buf.in.requested_size = dma_buf_alloc_size;
    dma_buf.in.buf_index = 0;

    if (ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dma_buf)) {
        log_debug(LogUMD, "Failed to allocate DMA buffer: {}", strerror(errno));
    } else {
        // OK - we have a buffer.  Map it.
        void *buffer = mmap(
            nullptr,
            dma_buf_alloc_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            pci_device_file_desc,
            dma_buf.out.mapping_offset);

        if (buffer == MAP_FAILED) {
            // Similar rationale to above, although this is worse because we
            // can't deallocate it.  That only happens when we close the fd.
            log_error(LogUMD, "Failed to map DMA buffer: {}", strerror(errno));
            return false;
        } else {
            log_debug(
                LogUMD, "Allocated PCIe DMA buffer of size {} for PCI device {}.", dma_buf_alloc_size, pci_device_num);
            dma_buffer.buffer = static_cast<uint8_t *>(buffer);
            dma_buffer.completion = static_cast<uint8_t *>(buffer) + dma_buf_size;
            dma_buffer.buffer_pa = dma_buf.out.physical_address;
            dma_buffer.completion_pa = dma_buf.out.physical_address + dma_buf_size;
            dma_buffer.size = dma_buf_size;
            return true;
        }
    }

    return false;
}

void PCIDevice::allocate_pcie_dma_buffer() {
    if (arch != tt::ARCH::WORMHOLE_B0) {
        // DMA buffer is only supported on Wormhole B0.
        return;
    }
    // DMA buffer allocation.
    // Allocation tries to allocate larger DMA buffers first. Starting size depends on whether IOMMU is enabled or not.
    // If IOMMU is enabled, we will try to allocate 16MB buffer first.
    // If IOMMU is not enabled, we will try to allocate 2MB buffer first.
    // If that fails, we will try smaller sizes until we can't allocate even single page.
    // + 0x1000 is for the completion page.  Since this entire implementation
    // is a temporary hack until it's implemented in the driver, we'll need to
    // poll a completion page to know when the DMA is done instead of receiving
    // an interrupt.
    uint32_t dma_buf_size;
    static const uint32_t page_size = static_cast<uint32_t>(sysconf(_SC_PAGESIZE));
    const uint32_t one_mb = 1 << 20;
    if (is_iommu_enabled()) {
        dma_buf_size = 16 * one_mb;
    } else {
        dma_buf_size = 2 * one_mb;
    }

    while (dma_buf_size >= page_size) {
        bool dma_buf_allocation_success = false;

        if (is_iommu_enabled()) {
            dma_buf_allocation_success = try_allocate_pcie_dma_buffer_iommu(dma_buf_size);
        } else {
            dma_buf_allocation_success = try_allocate_pcie_dma_buffer_no_iommu(dma_buf_size);
        }

        if (dma_buf_allocation_success) {
            break;
        }

        dma_buf_size >>= 1;
    }
}

tt::ARCH PCIDevice::get_pcie_arch() {
    static bool enumerated_devices = false;
    static tt::ARCH cached_arch = tt::ARCH::Invalid;
    if (!enumerated_devices) {
        auto devices = PCIDevice::enumerate_devices_info();
        if (devices.empty()) {
            return tt::ARCH::Invalid;
        }
        enumerated_devices = true;
        cached_arch = devices.begin()->second.get_arch();
        return cached_arch;
    }

    return cached_arch;
}

bool PCIDevice::is_arch_agnostic_reset_supported() {
    if (PCIDevice::read_kmd_version() >= KMD_ARCH_AGNOSTIC_RESET) {
        return true;
    }
    return false;
}

}  // namespace tt::umd
