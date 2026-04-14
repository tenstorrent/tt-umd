// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_umd/pcie/pci_device.hpp"

#include <fcntl.h>      // for ::open
#include <linux/pci.h>  // for PCI_SLOT, PCI_FUNC
#include <sys/mman.h>   // for mmap, munmap
#include <unistd.h>     // for ::close

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>  // for memcpy
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

#include "pci_ids.h"
#include "tt_kmd_lib.h"
#include "tt_umd/arch/architecture_implementation.hpp"
#include "tt_umd/types/arch.hpp"
#include "tt_umd_common/assert.hpp"
#include "tt_umd_common/tracy.hpp"
#include "tt_umd_common/utils.hpp"
#include "tt_umd_common/utils/common.hpp"
#include "tt_umd_common/utils/kmd_versions.hpp"

namespace tt::umd {

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

    // Handle hexadecimal input for integer types.
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

static std::optional<int> get_physical_slot_for_pcie_bdf(const std::string &target_bdf) {
    std::string base_path = "/sys/bus/pci/slots";

    for (const auto &entry : std::filesystem::directory_iterator(base_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        std::string dir_name = entry.path().filename().string();
        if (!tt::umd::utils::is_integer_string(dir_name)) {
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

PciDeviceInfo PCIDevice::read_device_info(int fd) {
    tt_device_info_t info{};

    if (tt_device_get_info_fd(fd, &info) != 0) {
        TT_THROW("tt_device_get_info_fd failed");
    }

    uint16_t bus = info.bus_dev_fn >> 8;
    uint16_t dev = (info.bus_dev_fn >> 3) & 0x1F;
    uint16_t fn = info.bus_dev_fn & 0x07;

    std::string pci_bdf = get_pci_bdf(info.pci_domain, bus, dev, fn);

    return PciDeviceInfo{
        info.vendor_id,
        info.device_id,
        info.subsystem_vendor_id,
        info.subsystem_id,
        info.pci_domain,
        bus,
        dev,
        fn,
        pci_bdf,
        get_physical_slot_for_pcie_bdf(pci_bdf)};
}

static void reset_device_ioctl(const std::unordered_set<int> &pci_target_devices, uint32_t flags) {
    for (int n : PCIDevice::enumerate_devices()) {
        if (!pci_target_devices.empty() && pci_target_devices.find(n) == pci_target_devices.end()) {
            continue;
        }

        log_debug(tt::LogUMD, "Issuing reset ioctl on PCI device ID {} with flags {}", n, flags);

        tt_device_t *dev = nullptr;
        if (tt_device_open(fmt::format("/dev/tenstorrent/{}", n).c_str(), &dev, O_APPEND) != 0) {
            continue;
        }

        try {
            int ret = tt_device_reset(dev, flags);
            if (ret != 0) {
                TT_THROW("tt_device_reset failed on device {} with flags {}: {}", n, flags, strerror(-ret));
            }
        } catch (const std::exception &e) {
            log_error(tt::LogUMD, "Reset IOCTL failed: {}", e.what());
        } catch (...) {
            log_error(tt::LogUMD, "Reset IOCTL failed with unknown error");
        }

        tt_device_close(dev);
    }
}

tt::ARCH PciDeviceInfo::get_arch() const {
    if (this->device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (this->device_id == TT_BLACKHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::BLACKHOLE;
    }
    return tt::ARCH::Invalid;
}

std::vector<int> PCIDevice::enumerate_devices() {
    ZoneScopedC(tracy::Color::DarkGreen);
    std::vector<int> device_ids;
    std::string path = "/dev/tenstorrent/";

    if (!std::filesystem::exists(path)) {
        return device_ids;
    }

    const char *tt_visible_devices_env = std::getenv("TT_VISIBLE_DEVICES");
    if (!tt_visible_devices_env) {
        return sort_ids_based_on_bdf(get_all_device_ids());
    }

    std::string tt_visible_devices_str(tt_visible_devices_env);
    if (tt_visible_devices_str.empty()) {
        return device_ids;
    }

    std::vector<std::string> device_tokens = utils::split_string_by_comma(tt_visible_devices_str);

    std::map<std::string, int> bdf_to_device_id_map = get_bdf_to_device_id_map();

    std::vector<int> all_device_ids = {};

    for (const auto &[bdf, device_id] : get_bdf_to_device_id_map()) {
        all_device_ids.push_back(device_id);
    }

    std::set<int> filtered_device_ids;

    for (const auto &device_token : device_tokens) {
        // Check if token is BDF format (contains colon and dot).
        bool is_bdf = tt::umd::utils::is_bdf_string(device_token);

        if (is_bdf) {
            bool matched_bdf_pattern = false;
            for (const auto &bdf_to_device_id : bdf_to_device_id_map) {
                if (bdf_to_device_id.first.find(device_token) != std::string::npos) {
                    int device_id = bdf_to_device_id.second;
                    filtered_device_ids.insert(device_id);
                    log_debug(
                        LogUMD,
                        "Added device id {} with BDF {} because of token filter {}.",
                        device_id,
                        bdf_to_device_id.first,
                        device_token);
                    matched_bdf_pattern = true;
                }
            }

            if (!matched_bdf_pattern) {
                TT_THROW(
                    "Invalid BDF identifier in TT_VISIBLE_DEVICES: {}. Valid device identifiers are either integers or "
                    "part of the BDF string.",
                    device_token);
            }

            continue;
        }

        bool is_integer = tt::umd::utils::is_integer_string(device_token);

        if (is_integer) {
            int logical_device_id = std::stoi(device_token);

            if (logical_device_id < 0 || logical_device_id >= all_device_ids.size()) {
                TT_THROW(
                    "Invalid device ID in TT_VISIBLE_DEVICES: {}.  Valid device identifiers are either integers or "
                    "part of the BDF string. Valid integer IDs are between 0 and {}.",
                    device_token,
                    all_device_ids.size() - 1);
            }

            log_debug(
                LogUMD,
                "Added device id {} because of token filter {}.",
                all_device_ids[logical_device_id],
                device_token);

            filtered_device_ids.insert(all_device_ids[logical_device_id]);

        } else {
            TT_THROW(
                "Invalid device identifier in TT_VISIBLE_DEVICES: {}.  Valid device identifiers are either integers or "
                "part of the BDF string.",
                device_token);
        }
    }

    for (const int &filtered_device_id : filtered_device_ids) {
        device_ids.push_back(filtered_device_id);
    }

    return sort_ids_based_on_bdf(device_ids);
}

std::vector<int> PCIDevice::sort_ids_based_on_bdf(const std::vector<int> &pci_device_ids) {
    std::vector<int> sorted_ids_based_on_bdf;
    std::map<std::string, int> bdf_to_device_id_map = get_bdf_to_device_id_map();
    std::unordered_set<int> input_ids(pci_device_ids.begin(), pci_device_ids.end());
    std::unordered_set<int> mapped_ids;

    for (const auto &[bdf, device_id] : bdf_to_device_id_map) {
        if (input_ids.count(device_id)) {
            sorted_ids_based_on_bdf.push_back(device_id);
            mapped_ids.insert(device_id);
        }
    }

    // Append any IDs that could not be mapped to a BDF, preserving input order.
    for (int device_id : pci_device_ids) {
        if (!mapped_ids.count(device_id)) {
            log_debug(tt::LogUMD, "Device ID {} could not be mapped to a BDF, appending at end.", device_id);
            sorted_ids_based_on_bdf.push_back(device_id);
        }
    }

    return sorted_ids_based_on_bdf;
}

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    std::map<int, PciDeviceInfo> infos;
    for (int n : PCIDevice::enumerate_devices()) {
        int fd = open(fmt::format("/dev/tenstorrent/{}", n).c_str(), O_RDWR | O_CLOEXEC | O_APPEND);
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

std::optional<int> PCIDevice::get_pci_device_id(int umd_logical_id) {
    std::vector<int> enumerated_ids = PCIDevice::enumerate_devices();
    if (umd_logical_id < 0 || umd_logical_id >= static_cast<int>(enumerated_ids.size())) {
        return std::nullopt;
    }
    return enumerated_ids[umd_logical_id];
}

static int open_pci_device(const std::string &device_path) {
    // O_APPEND opts out of legacy mode in KMD >= 2.6.0, allowing the device to enter low-power idle states.
    int flags = O_RDWR | O_CLOEXEC;
    if (PCIDevice::read_kmd_version() >= KMD_POWER_STATE && PCIDevice::get_pcie_arch() == tt::ARCH::BLACKHOLE) {
        log_debug(LogUMD, fmt::format("Opening device {} in power aware mode.", device_path));
        flags |= O_APPEND;
    } else {
        log_debug(LogUMD, fmt::format("Opening device {} in legacy mode regarding device power.", device_path));
    }
    return open(device_path.c_str(), flags);
}

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(fmt::format("/dev/tenstorrent/{}", pci_device_number)),
    pci_device_num(pci_device_number),
    pci_device_file_desc(open_pci_device(device_path)),
    info(read_device_info(pci_device_file_desc)),
    numa_node(read_sysfs<int>(info, "numa_node", -1)),  // default to -1 if not found
    revision(read_sysfs<int>(info, "revision")),
    arch(info.get_arch()),
    kmd_version(PCIDevice::read_kmd_version()),
    iommu_enabled(detect_iommu(info)),
    arch_impl_(architecture_implementation::create(arch)) {
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

    int extra_flags = (kmd_version >= KMD_POWER_STATE) ? O_APPEND : 0;
    int ret_code = tt_device_open(device_path.c_str(), &tt_device_handle, extra_flags);

    if (ret_code != 0) {
        if (tt_device_handle != nullptr) {
            tt_device_close(tt_device_handle);
        }
        TT_THROW(
            "tt_device_open failed with error code {} for PCI device with device ID {}.", ret_code, pci_device_number);
    }

    {
        uint64_t api_version = 0;
        tt_driver_get_attr(tt_device_handle, TT_DRIVER_API_VERSION, &api_version);
        log_debug(
            LogUMD,
            "Opened PCI device {}; KMD version: {}; API: {}; IOMMU: {}",
            pci_device_num,
            kmd_version.to_string(),
            api_version,
            iommu_enabled ? "enabled" : "disabled");
    }

    TT_ASSERT(arch != tt::ARCH::WORMHOLE_B0 || revision == 0x01, "Wormhole B0 must have revision 0x01");

    tt_bar_mapping_t bar_mappings[8]{};
    uint32_t bar_mapping_count = 0;
    if (tt_query_mappings(tt_device_handle, bar_mappings, 8, &bar_mapping_count) != 0) {
        throw std::runtime_error(fmt::format("Query mappings failed on device {}.", pci_device_num));
    }

    // Mapping resource to BAR
    // Resource 0 -> BAR0
    // Resource 1 -> BAR2
    // Resource 2 -> BAR4.
    tt_bar_mapping_t bar0_uc_mapping{};
    tt_bar_mapping_t bar2_uc_mapping{};
    tt_bar_mapping_t bar2_wc_mapping{};
    tt_bar_mapping_t bar4_uc_mapping{};

    for (unsigned int i = 0; i < bar_mapping_count; i++) {
        log_trace(
            LogUMD,
            "BAR mapping id {} base {} size {}",
            bar_mappings[i].mapping_id,
            reinterpret_cast<void *>(bar_mappings[i].mapping_base),
            bar_mappings[i].mapping_size);

        switch (bar_mappings[i].mapping_id) {
            case TT_BAR_MAPPING_RESOURCE0_UC:
                bar0_uc_mapping = bar_mappings[i];
                break;
            case TT_BAR_MAPPING_RESOURCE1_UC:
                bar2_uc_mapping = bar_mappings[i];
                break;
            case TT_BAR_MAPPING_RESOURCE1_WC:
                bar2_wc_mapping = bar_mappings[i];
                break;
            case TT_BAR_MAPPING_RESOURCE2_UC:
                bar4_uc_mapping = bar_mappings[i];
                break;
            default:
                break;
        }
    }

    if (bar0_uc_mapping.mapping_id != TT_BAR_MAPPING_RESOURCE0_UC) {
        throw std::runtime_error(fmt::format("Device {} has no BAR0 UC mapping.", pci_device_num));
    }

    bar0 = mmap(
        nullptr,
        PCIDevice::bar0_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        pci_device_file_desc,
        bar0_uc_mapping.mapping_base + PCIDevice::bar0_mapping_offset);

    if (bar0 == MAP_FAILED) {
        throw std::runtime_error(fmt::format("BAR0 mapping failed for device {}.", pci_device_num));
    }

    // Map TLB configuration registers. Wormhole has up to 186 TLBs and Blackhole up to 202 TLBs; with
    // approximately 8–12 bytes per TLB configuration register, the maximum required space is about
    // 202 * 12 = 2424 bytes, which fits comfortably in a single 4 KB page.
    tlb_config_space = mmap(
        nullptr,
        tlb_config_space_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        pci_device_file_desc,
        bar0_uc_mapping.mapping_base + arch_impl_->get_static_tlb_cfg_addr());

    if (tlb_config_space == MAP_FAILED) {
        throw std::runtime_error(
            fmt::format("TLB configuration registers mapping failed for device {}.", pci_device_num));
    }

    if (arch == tt::ARCH::WORMHOLE_B0) {
        if (bar4_uc_mapping.mapping_id != TT_BAR_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 UC mapping.", pci_device_num));
        }

        bar2_uc_size = bar2_uc_mapping.mapping_size;
        bar2_uc = mmap(
            nullptr,
            bar2_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            pci_device_file_desc,
            bar2_uc_mapping.mapping_base);

        if (bar2_uc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR2 UC mapping failed for device {}.", pci_device_num));
        }
    } else if (arch == tt::ARCH::BLACKHOLE) {
        if (bar2_uc_mapping.mapping_id != TT_BAR_MAPPING_RESOURCE1_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR2 UC mapping.", pci_device_num));
        }

        // Using UnCachable memory mode. This is used for accessing registers on Blackhole.
        bar2_uc_size = bar2_uc_mapping.mapping_size;
        bar2_uc = mmap(
            nullptr,
            bar2_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            pci_device_file_desc,
            bar2_uc_mapping.mapping_base);

        if (bar2_uc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR2 UC mapping failed for device {}.", pci_device_num));
        }
    }

    allocate_pcie_dma_buffer();
}

PCIDevice::~PCIDevice() {
    int ret_code = tt_device_close(tt_device_handle);

    if (ret_code != 0) {
        log_warning(
            LogUMD,
            "tt_device_close failed with error code {} for PCI device with device ID {}.",
            ret_code,
            pci_device_num);
    }

    close(pci_device_file_desc);

    if (bar0 != nullptr && bar0 != MAP_FAILED) {
        munmap(bar0, bar0_size);
    }

    if (tlb_config_space != nullptr && tlb_config_space != MAP_FAILED) {
        munmap(tlb_config_space, tlb_config_space_size);
    }

    if (bar2_uc != nullptr && bar2_uc != MAP_FAILED) {
        munmap(bar2_uc, bar2_uc_size);
    }

    if (dma_buffer.buffer != nullptr && dma_buffer.buffer != MAP_FAILED) {
        munmap(dma_buffer.buffer, dma_buffer.size + 0x1000);
    }
}

uint64_t PCIDevice::map_for_hugepage(void *buffer, size_t size) {
    const uint64_t vaddr = reinterpret_cast<uint64_t>(buffer);
    uint64_t pa = 0;

    int ret = tt_pin_pages(tt_device_handle, vaddr, size, TT_DMA_FLAG_CONTIGUOUS, &pa);
    if (ret != 0) {
        log_warning(
            LogUMD,
            "Failed to pin pages for hugepage at virtual address {:#x} with size {:#x}: {}",
            vaddr,
            size,
            strerror(-ret));
        return 0;
    }

    log_debug(
        LogUMD,
        "Pinning pages for Hugepage: virtual address {:#x} and size {:#x} pinned to physical address {:#x}",
        vaddr,
        size,
        pa);

    return pa;
}

bool PCIDevice::is_mapping_buffer_to_noc_supported() { return PCIDevice::read_kmd_version() >= KMD_MAP_TO_NOC; }

std::pair<uint64_t, uint64_t> PCIDevice::map_buffer_to_noc(void *buffer, size_t size) {
    if (PCIDevice::read_kmd_version() < KMD_MAP_TO_NOC) {
        TT_THROW("KMD version must be at least 2.0.0 to use buffer with NOC mapping");
    }

    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t vaddr = reinterpret_cast<uint64_t>(buffer);

    if (vaddr % page_size != 0 || size % page_size != 0) {
        TT_THROW("Buffer must be page-aligned with a size that is a multiple of the page size");
    }

    if (size > page_size && !is_iommu_enabled()) {
        TT_THROW("Cannot map buffer of size {} to NOC with IOMMU disabled", size);
    }

    uint64_t noc_addr = 0, pa = 0;
    int ret = tt_pin_pages_noc(tt_device_handle, vaddr, size, TT_DMA_FLAG_NONE, &noc_addr, &pa);
    if (ret != 0) {
        TT_THROW(
            "Failed to pin pages for DMA buffer at virtual address {:#x} with size {:#x}: {}",
            vaddr,
            size,
            strerror(-ret));
    }

    log_debug(
        LogUMD,
        "Pinning pages for DMA: virtual address {:#x} and size {:#x} pinned to physical address {:#x} and mapped to "
        "noc address {:#x}",
        vaddr,
        size,
        pa,
        noc_addr);

    return {noc_addr, pa};
}

std::pair<uint64_t, uint64_t> PCIDevice::map_hugepage_to_noc(void *hugepage, size_t size) {
    if (PCIDevice::read_kmd_version() < KMD_MAP_TO_NOC) {
        TT_THROW("KMD version must be at least 2.0.0 to use hugepages with NOC mapping");
    }

    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t vaddr = reinterpret_cast<uint64_t>(hugepage);

    if (size > (1 << 30)) {
        TT_THROW("Not a hugepage");
    }

    if (vaddr % page_size != 0 || size % page_size != 0) {
        TT_THROW("Buffer must be page-aligned with a size that is a multiple of the page size");
    }

    if (is_iommu_enabled()) {
        // IOMMU is enabled, so we don't need huge pages.
        log_warning(LogUMD, "Mapping a hugepage with IOMMU enabled.");
    }

    uint64_t noc_addr = 0, pa = 0;
    int ret = tt_pin_pages_noc(tt_device_handle, vaddr, size, TT_DMA_FLAG_CONTIGUOUS, &noc_addr, &pa);
    if (ret != 0) {
        TT_THROW(
            "Failed to pin pages for hugepage at virtual address {:#x} with size {:#x}: {}",
            vaddr,
            size,
            strerror(-ret));
    }

    log_debug(
        LogUMD,
        "Pinning pages for Hugepage: virtual address {:#x} and size {:#x} pinned to physical address {:#x} and mapped "
        "to noc address {:#x}",
        vaddr,
        size,
        pa,
        noc_addr);

    return {noc_addr, pa};
}

uint64_t PCIDevice::map_for_dma(void *buffer, size_t size) {
    static const auto page_size = sysconf(_SC_PAGESIZE);

    const uint64_t vaddr = reinterpret_cast<uint64_t>(buffer);
    const int flags = is_iommu_enabled() ? TT_DMA_FLAG_NONE : TT_DMA_FLAG_CONTIGUOUS;

    if (vaddr % page_size != 0 || size % page_size != 0) {
        TT_THROW("Buffer must be page-aligned with a size that is a multiple of the page size");
    }

    uint64_t pa = 0;
    int ret = tt_pin_pages(tt_device_handle, vaddr, size, flags, &pa);
    if (ret != 0) {
        TT_THROW(
            "Failed to pin pages for DMA buffer at virtual address {:#x} with size {:#x}: {}",
            vaddr,
            size,
            strerror(-ret));
    }

    log_debug(
        LogUMD,
        "Pinning pages for DMA: virtual address {:#x} and size {:#x} pinned to physical address {:#x} without mapping "
        "to noc",
        vaddr,
        size,
        pa);

    return pa;
}

void PCIDevice::unmap_for_dma(void *buffer, size_t size) {
    static const auto page_size = sysconf(_SC_PAGESIZE);

    const uint64_t vaddr = reinterpret_cast<uint64_t>(buffer);

    if (vaddr % page_size != 0 || size % page_size != 0) {
        TT_THROW("Buffer must be page-aligned with a size that is a multiple of the page size");
    }

    int ret = tt_unpin_pages(tt_device_handle, vaddr, size);
    if (ret != 0) {
        TT_THROW(
            "Failed to unpin pages for DMA buffer at virtual address {:#x} and size {:#x}: {}",
            vaddr,
            size,
            strerror(-ret));
    }

    log_debug(LogUMD, "Unpinning pages for DMA: virtual address {:#x} and size {:#x}", vaddr, size);
}

SemVer PCIDevice::read_kmd_version() {
    static const std::string path = "/sys/module/tenstorrent/version";
    std::ifstream file(path);

    if (!file.is_open()) {
        log_warning(LogUMD, "Failed to open file: {}", path);
        return {0, 0, 0};
    }

    std::string version_str;
    std::getline(file, version_str);

    return SemVer(version_str);
}

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(const size_t tlb_size, const TlbMapping tlb_mapping) {
    try {
        return std::make_unique<SiliconTlbHandle>(*this, tlb_size, tlb_mapping);
    } catch (const std::exception &e) {
        if (read_kmd_version() < SemVer(2, 6, 0)) {
            TT_THROW(
                "Failed to allocate TLB window. Note that the resource might be exhausted by some other hung process. "
                "Error: {}",
                e.what());
        }
        TT_THROW(
            "Failed to allocate TLB window. Look at /sys/kernel/debug/tenstorrent/{}/mappings and "
            "/proc/driver/tenstorrent/{}/pids for more information. Error: {}",
            pci_device_num,
            pci_device_num,
            e.what());
    }
}

void PCIDevice::configure_tlb(const uint32_t tlb_index, const tlb_data &tlb_config) {
    // Get the TLB configuration for this index.
    auto tlb_configuration = arch_impl_->get_tlb_configuration(tlb_index);

    // Apply the architecture-specific bit field offsets to pack the TLB data.
    auto [lower_64, upper_64] = tlb_config.apply_offset(tlb_configuration.offset);

    // Calculate the register address for this TLB index using architecture-specific register size.
    const uint64_t tlb_cfg_reg_size_bytes = arch_impl_->get_tlb_cfg_reg_size_bytes();
    uint64_t tlb_register_addr = tlb_index * tlb_cfg_reg_size_bytes;

    // Write to the appropriate location in BAR0.
    volatile uint64_t *tlb_reg_ptr =
        reinterpret_cast<volatile uint64_t *>(static_cast<char *>(tlb_config_space) + tlb_register_addr);

    // Write the TLB register values
    // Wormhole uses 64-bit registers (8 bytes), Blackhole uses 96-bit registers (12 bytes).
    tlb_reg_ptr[0] = lower_64;

    if (arch == tt::ARCH::BLACKHOLE) {
        // Blackhole needs the upper 32 bits as well (96-bit total)
        // Cast to uint32_t* to write only 4 bytes and avoid overwriting the next register.
        volatile uint32_t *tlb_reg_upper_ptr = reinterpret_cast<volatile uint32_t *>(tlb_reg_ptr);
        tlb_reg_upper_ptr[2] = static_cast<uint32_t>(upper_64);  // Write to bytes 8-11
    }

    log_trace(
        LogUMD,
        "Configured TLB index {} at address 0x{:x} with lower=0x{:x}, upper=0x{:x}",
        tlb_index,
        tlb_register_addr,
        lower_64,
        upper_64);
}

void PCIDevice::reset_device_ioctl(const std::unordered_set<int> &pci_target_devices, TenstorrentResetDevice flag) {
    umd::reset_device_ioctl(pci_target_devices, static_cast<uint32_t>(flag));
}

uint8_t PCIDevice::read_command_byte(const int pci_device_num) {
    int fd = open_pci_device(fmt::format("/dev/tenstorrent/{}", pci_device_num));
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
    const uint64_t dma_buf_alloc_size = dma_buf_size + 0x1000;  // + 0x1000 for completion page

    uint64_t pa = 0, mmap_offset = 0;
    uint32_t actual_size = 0;
    int ret = tt_allocate_dma_buf(tt_device_handle, dma_buf_alloc_size, 0, &pa, &mmap_offset, &actual_size);

    if (ret != 0) {
        log_debug(LogUMD, "Failed to allocate DMA buffer: {}", strerror(-ret));
    } else {
        // OK - we have a buffer.  Map it.
        void *buffer =
            mmap(nullptr, dma_buf_alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, mmap_offset);

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
            dma_buffer.buffer_pa = pa;
            dma_buffer.completion_pa = pa + dma_buf_size;
            dma_buffer.size = dma_buf_size;
            return true;
        }
    }

    return false;
}

void PCIDevice::allocate_pcie_dma_buffer() {
    if (arch != tt::ARCH::WORMHOLE_B0 && arch != tt::ARCH::BLACKHOLE) {
        // DMA buffer is only supported on Wormhole B0 and Blackhole.
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

bool PCIDevice::is_arch_agnostic_reset_supported() { return PCIDevice::read_kmd_version() >= KMD_ARCH_AGNOSTIC_RESET; }

void PCIDevice::set_power_state(bool busy) {
    if (arch != tt::ARCH::BLACKHOLE) {
        return;
    }

    if (kmd_version < KMD_POWER_STATE) {
        log_warning(LogUMD, "KMD version {} does not support power state management.", kmd_version.to_string());
        return;
    }

    uint16_t power_flags = 0;
    if (busy) {
        power_flags = TT_POWER_FLAG_MRISC_PHY_WAKEUP | TT_POWER_FLAG_TENSIX_ENABLE | TT_POWER_FLAG_L2CPU_ENABLE;
    }

    int ret = tt_set_power_state(tt_device_handle, power_flags);
    if (ret != 0) {
        log_warning(LogUMD, "tt_set_power_state failed on device {}: {}", pci_device_num, strerror(-ret));
    }
}

std::vector<int> PCIDevice::get_all_device_ids() {
    std::vector<int> device_ids;
    std::string path = "/dev/tenstorrent/";

    if (!std::filesystem::exists(path)) {
        return device_ids;
    }

    // Enumerate all devices, ignoring TT_VISIBLE_DEVICES.
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        std::string filename = entry.path().filename().string();
        if (tt::umd::utils::is_integer_string(filename)) {
            int pci_device_id = std::stoi(filename);
            device_ids.push_back(pci_device_id);
        }
    }

    return device_ids;
}

std::map<std::string, int> PCIDevice::get_bdf_to_device_id_map() {
    std::map<std::string, int> bdf_to_device_id;

    for (int device_id : get_all_device_ids()) {
        int fd = open(fmt::format("/dev/tenstorrent/{}", device_id).c_str(), O_RDWR | O_CLOEXEC | O_APPEND);
        if (fd == -1) {
            continue;
        }

        try {
            PciDeviceInfo device_info = read_device_info(fd);
            bdf_to_device_id[device_info.pci_bdf] = device_id;
        } catch (...) {
            // Ignore failed reads and continue with next device.
        }

        close(fd);
    }

    return bdf_to_device_id;
}

}  // namespace tt::umd
