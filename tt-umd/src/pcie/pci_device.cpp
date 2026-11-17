// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/pci_device.hpp"

#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/utils.hpp"
#include "tt-kmd-lib/pci_ids.h"
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd-utils/internal/host.hpp"
#include "tt-umd-utils/internal/string_utils.hpp"
#include "tt-umd/arch/architecture_registers.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/driver_atomics.hpp"
#include "tt-umd/pcie/silicon_tlb_handle.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/kmd_versions.hpp"

namespace tt::umd {

namespace {

constexpr const char *DEVICE_DIRECTORY = "/dev/tenstorrent";
constexpr const char *PCI_SLOTS_DIRECTORY = "/sys/bus/pci/slots";

std::string device_path_for(int pci_device_num) { return fmt::format("{}/{}", DEVICE_DIRECTORY, pci_device_num); }

// The physical slot whose "address" file names this device's bus and device ("dddd:bb:dd"), if the platform
// describes its slots at all.
std::optional<int> find_physical_slot(const std::string &pci_bdf) {
    const std::string slot_address = pci_bdf.substr(0, pci_bdf.rfind('.'));
    std::error_code ec;
    for (const auto &slot : std::filesystem::directory_iterator(PCI_SLOTS_DIRECTORY, ec)) {
        std::ifstream address_file(slot.path() / "address");
        std::string address;
        if (!(address_file >> address) || address != slot_address) {
            continue;
        }
        const std::string slot_name = slot.path().filename().string();
        if (utils::is_integer_string(slot_name)) {
            return std::stoi(slot_name);
        }
    }
    return std::nullopt;
}

// Owns a device handle opened only to query the device, so it is closed on every path.
struct DeviceHandleCloser {
    void operator()(tt_device_t *handle) const { tt_device_close(handle); }
};

using DeviceHandle = std::unique_ptr<tt_device_t, DeviceHandleCloser>;

DeviceHandle open_device_handle(const std::string &device_path) {
    tt_device_t *handle = nullptr;
    const int result = tt_device_open(device_path.c_str(), &handle, 0);
    if (result != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to open {} (error {}).", device_path, result));
    }
    return DeviceHandle(handle);
}

// Device numbers N of the /dev/tenstorrent/N nodes, in ascending numeric order.
std::vector<int> list_device_numbers() {
    std::vector<int> device_nums;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(DEVICE_DIRECTORY, ec)) {
        const std::string name = entry.path().filename().string();
        if (utils::is_integer_string(name)) {
            device_nums.push_back(std::stoi(name));
        }
    }
    std::sort(device_nums.begin(), device_nums.end());
    return device_nums;
}

// Info of each device in device_nums that can be opened and queried. The others are left out, so one broken node
// does not hide the healthy devices.
std::map<int, PciDeviceInfo> read_device_infos(const std::vector<int> &device_nums) {
    std::map<int, PciDeviceInfo> infos;
    for (const int device_num : device_nums) {
        try {
            infos.emplace(device_num, PCIDevice::read_device_info(device_path_for(device_num)));
        } catch (const std::exception &e) {
            log_debug(LogUMD, "Cannot read the info of {}: {}", device_path_for(device_num), e.what());
        }
    }
    return infos;
}

// device_nums in BDF order, followed by the ones missing from infos in their original order.
std::vector<int> order_by_bdf(const std::vector<int> &device_nums, const std::map<int, PciDeviceInfo> &infos) {
    std::vector<int> ordered = device_nums;
    const auto unreadable_begin = std::stable_partition(
        ordered.begin(), ordered.end(), [&](int device_num) { return infos.count(device_num) != 0; });
    std::sort(ordered.begin(), unreadable_begin, [&](int lhs, int rhs) {
        return infos.at(lhs).pci_bdf < infos.at(rhs).pci_bdf;
    });
    return ordered;
}

// Devices named by the TT_VISIBLE_DEVICES value. An integer token is a position in all_devices, the device numbers in
// numeric order; any other token is a BDF fragment matching every device whose BDF contains it.
std::vector<int> select_visible_devices(
    const std::vector<int> &all_devices, const std::map<int, PciDeviceInfo> &infos, const std::string &value) {
    std::set<int> selected;
    for (const std::string &token : utils::split_string_by_comma(value)) {
        if (utils::is_integer_string(token)) {
            // An index too large for size_t is just as out of range as one past the end.
            size_t index = std::numeric_limits<size_t>::max();
            try {
                index = std::stoul(token);
            } catch (const std::out_of_range &) {
            }
            if (index >= all_devices.size()) {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format(
                        "Invalid device index {} in {}: {} devices are available.",
                        token,
                        utils::TT_VISIBLE_DEVICES_ENV,
                        all_devices.size()));
            }
            selected.insert(all_devices[index]);
        } else if (utils::is_bdf_string(token)) {
            // BDFs are formatted in lowercase hex.
            std::string bdf_fragment = token;
            std::transform(bdf_fragment.begin(), bdf_fragment.end(), bdf_fragment.begin(), [](unsigned char c) {
                return std::tolower(c);
            });
            bool matched = false;
            for (const auto &[device_num, info] : infos) {
                if (info.pci_bdf.find(bdf_fragment) != std::string::npos) {
                    selected.insert(device_num);
                    matched = true;
                }
            }
            if (!matched) {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("No device's BDF matches {} in {}.", token, utils::TT_VISIBLE_DEVICES_ENV));
            }
        } else {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Invalid device identifier {} in {}: expected a device index or a BDF.",
                    token,
                    utils::TT_VISIBLE_DEVICES_ENV));
        }
    }
    return {selected.begin(), selected.end()};
}

// The devices enumerate_devices() returns, together with the info of every device that could be read.
std::pair<std::vector<int>, std::map<int, PciDeviceInfo>> enumerate_visible_devices() {
    const std::vector<int> all_devices = list_device_numbers();
    std::map<int, PciDeviceInfo> infos = read_device_infos(all_devices);
    const std::optional<std::string> visible_devices = utils::get_env_var_value(utils::TT_VISIBLE_DEVICES_ENV.data());
    if (!visible_devices.has_value()) {
        return {order_by_bdf(all_devices, infos), std::move(infos)};
    }
    std::vector<int> visible = order_by_bdf(select_visible_devices(all_devices, infos, *visible_devices), infos);
    log_debug(
        LogUMD,
        "{}={} leaves {} of {} devices.",
        utils::TT_VISIBLE_DEVICES_ENV,
        *visible_devices,
        visible.size(),
        all_devices.size());
    return {std::move(visible), std::move(infos)};
}

constexpr const char *KMD_VERSION_PATH = "/sys/module/tenstorrent/version";

// A TLB configuration write reaches the register within one PCIe round trip; this only bounds a dead link.
constexpr auto TLB_CONFIG_VERIFY_TIMEOUT = std::chrono::milliseconds(100);

// Largest PCIe DMA buffer tried first; allocation halves the size on failure down to one page.
constexpr size_t MAX_PCIE_DMA_BUFFER_SIZE = 2 * (1 << 20);

std::filesystem::path sysfs_device_path(const PciDeviceInfo &info) {
    return std::filesystem::path("/sys/bus/pci/devices") / info.pci_bdf;
}

std::optional<std::string> read_first_token(const std::filesystem::path &path) {
    std::ifstream file(path);
    std::string token;
    if (!(file >> token)) {
        return std::nullopt;
    }
    return token;
}

// An integer sysfs attribute of the device, in the base it is printed in (0 lets "0x" prefixes pick hex).
std::optional<long> read_sysfs_integer(const PciDeviceInfo &info, const char *attribute, int base = 10) {
    const std::optional<std::string> token = read_first_token(sysfs_device_path(info) / attribute);
    if (!token.has_value()) {
        return std::nullopt;
    }
    try {
        return std::stol(*token, nullptr, base);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

int page_size() { return getpagesize(); }

bool is_page_aligned(uint64_t value) { return value % page_size() == 0; }

// The pin flags for a mapping the device may only read from, or read and write.
int dma_map_flags(DeviceBufferAccess device_access, int flags = TT_DMA_FLAG_NONE) {
    if (device_access == DeviceBufferAccess::READ_ONLY) {
        flags |= TT_DMA_FLAG_READ_ONLY;
    }
    return flags;
}

tt_noc_addr_config_t to_kmd_noc_config(const tlb_data &config) {
    tt_noc_addr_config_t kmd_config{};
    kmd_config.addr = config.local_offset;
    kmd_config.x_end = static_cast<uint16_t>(config.x_end);
    kmd_config.y_end = static_cast<uint16_t>(config.y_end);
    kmd_config.x_start = static_cast<uint16_t>(config.x_start);
    kmd_config.y_start = static_cast<uint16_t>(config.y_start);
    kmd_config.noc = static_cast<uint8_t>(config.noc_sel);
    kmd_config.mcast = static_cast<uint8_t>(config.mcast);
    kmd_config.ordering = static_cast<uint8_t>(config.ordering);
    kmd_config.static_vc = static_cast<uint8_t>(config.static_vc);
    return kmd_config;
}

bool maps_bar_windows(tt::ARCH arch) { return arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::BLACKHOLE; }

}  // namespace

tt::ARCH arch_from_pci_device_id(uint16_t device_id) {
    switch (device_id) {
        case TT_WORMHOLE_PCI_DEVICE_ID:
            return tt::ARCH::WORMHOLE_B0;
        case TT_BLACKHOLE_PCI_DEVICE_ID:
            return tt::ARCH::BLACKHOLE;
        case TT_QUASAR_PCI_DEVICE_ID:
            return tt::ARCH::QUASAR;
        default:
            return tt::ARCH::Invalid;
    }
}

tt::ARCH PciDeviceInfo::get_arch() const { return arch_from_pci_device_id(device_id); }

std::vector<int> PCIDevice::get_all_device_ids() { return list_device_numbers(); }

std::map<std::string, int> PCIDevice::get_bdf_to_device_id_map() {
    std::map<std::string, int> bdf_to_device_id;
    for (const auto &[device_num, info] : read_device_infos(list_device_numbers())) {
        bdf_to_device_id.emplace(info.pci_bdf, device_num);
    }
    return bdf_to_device_id;
}

std::vector<int> PCIDevice::sort_ids_based_on_bdf(const std::vector<int> &pci_device_ids) {
    return order_by_bdf(pci_device_ids, read_device_infos(pci_device_ids));
}

std::vector<int> PCIDevice::enumerate_devices() { return enumerate_visible_devices().first; }

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    const auto [visible, infos] = enumerate_visible_devices();
    std::map<int, PciDeviceInfo> visible_infos;
    for (const int device_num : visible) {
        // A device whose info cannot be read has nothing to report here.
        if (const auto info = infos.find(device_num); info != infos.end()) {
            visible_infos.emplace(*info);
        }
    }
    return visible_infos;
}

std::optional<int> PCIDevice::get_pci_device_id(int umd_logical_id) {
    const std::vector<int> devices = enumerate_devices();
    if (umd_logical_id < 0 || static_cast<size_t>(umd_logical_id) >= devices.size()) {
        return std::nullopt;
    }
    return devices[umd_logical_id];
}

PciDeviceInfo PCIDevice::read_device_info(const std::string &device_path) {
    const DeviceHandle handle = open_device_handle(device_path);
    tt_device_attrs_t attrs{};
    const int result = tt_device_get_attrs(handle.get(), &attrs);
    if (result != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to query {} (error {}).", device_path, result));
    }
    PciDeviceInfo info{};
    info.vendor_id = static_cast<uint16_t>(attrs.pci_vendor_id);
    info.device_id = static_cast<uint16_t>(attrs.pci_device_id);
    info.subsystem_vendor_id = static_cast<uint16_t>(attrs.pci_subsystem_vendor_id);
    info.subsystem_id = static_cast<uint16_t>(attrs.pci_subsystem_id);
    info.pci_domain = static_cast<uint16_t>(attrs.pci_domain);
    info.pci_bus = static_cast<uint16_t>(attrs.pci_bus);
    info.pci_device = static_cast<uint16_t>(attrs.pci_device);
    info.pci_function = static_cast<uint16_t>(attrs.pci_function);
    info.pci_bdf =
        fmt::format("{:04x}:{:02x}:{:02x}.{:x}", info.pci_domain, info.pci_bus, info.pci_device, info.pci_function);
    info.physical_slot = find_physical_slot(info.pci_bdf);
    return info;
}

bool PCIDevice::detect_iommu(const PciDeviceInfo &device_info) {
    // A device whose IOMMU group translates DMA ("DMA" or "DMA-FQ") is protected; "identity" is passthrough, and a
    // device without a group has no IOMMU in front of it at all.
    const std::optional<std::string> type = read_first_token(sysfs_device_path(device_info) / "iommu_group" / "type");
    return type.has_value() && type->rfind("DMA", 0) == 0;
}

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(device_path_for(pci_device_number)),
    pci_device_num(pci_device_number),
    info(read_device_info(device_path)),
    numa_node(static_cast<int>(read_sysfs_integer(info, "numa_node").value_or(-1))),
    revision(static_cast<int>(read_sysfs_integer(info, "revision", 0).value_or(0))),
    arch(info.get_arch()),
    kmd_version(read_kmd_version()),
    iommu_enabled(detect_iommu(info)),
    bar2_uc_size(0),
    read_checking_offset(0) {
    try {
        construct_resources();
    } catch (...) {
        release_resources();
        throw;
    }
}

PCIDevice::~PCIDevice() { release_resources(); }

void PCIDevice::construct_resources() {
    // O_APPEND opts out of the KMD's legacy power mode: the device may idle unless a client asks for power
    // through set_power_state().
    const int open_result = tt_device_open(device_path.c_str(), &tt_device_handle, O_APPEND);
    if (open_result != 0) {
        tt_device_handle = nullptr;
        UMD_THROW(error::RuntimeError, fmt::format("Failed to open {} (error {}).", device_path, open_result));
    }

    log_debug(
        LogUMD,
        "Opened {} ({}, {}, KMD {}, IOMMU {}).",
        device_path,
        info.pci_bdf,
        arch_to_str(arch),
        kmd_version.str(),
        iommu_enabled ? "on" : "off");

    // Quasar keeps its inbound apertures in the kernel, so there is nothing for userspace to map there.
    if (!maps_bar_windows(arch)) {
        return;
    }

    // tt-kmd-lib keeps its own descriptor private, and BAR mappings are made by mmap() on a descriptor of the
    // device, so the device gets a second one for them.
    pci_device_file_desc = open(device_path.c_str(), O_RDWR | O_CLOEXEC | O_APPEND);
    if (pci_device_file_desc < 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to open {}: {}.", device_path, strerror(errno)));
    }

    tt_bar_mappings_t bar_mappings{};
    const int query_result = tt_device_query_bar_mappings(tt_device_handle, &bar_mappings);
    const tt_bar_mapping_t &bar0_uc = bar_mappings.resource0_uc;
    const ArchitectureTlbs &tlbs = get_architecture_tlbs(arch);
    if (query_result != 0 || bar0_uc.id == TT_BAR_MAPPING_UNUSED ||
        bar0_uc.size <
            std::max<uint64_t>(bar0_mapping_offset + bar0_size, tlbs.static_cfg_addr + tlb_config_space_size)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "{} does not expose the expected BAR0 (error {}, size {:#x}).",
                device_path,
                query_result,
                bar0_uc.size));
    }

    auto map_bar0_range = [&](uint64_t offset, size_t size) {
        void *mapping =
            mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, bar0_uc.base + offset);
        if (mapping == MAP_FAILED) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Failed to map BAR0 range {:#x}+{:#x} of {}: {}.", offset, size, device_path, strerror(errno)));
        }
        return mapping;
    };

    // bar0 stands for the start of BAR0, so that BAR0 addresses index it directly; only its top bar0_size bytes
    // are mapped.
    void *arc_window = map_bar0_range(bar0_mapping_offset, bar0_size);
    bar0 = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(arc_window) - bar0_mapping_offset);
    tlb_config_space = map_bar0_range(tlbs.static_cfg_addr, tlb_config_space_size);
    read_checking_offset = get_architecture_registers(arch).noc_node_id_bar_offset;

    allocate_pcie_dma_buffer();
}

void PCIDevice::release_resources() {
    if (dma_buffer.buffer != nullptr) {
        // A KMD-allocated buffer is freed with the handle below; a pinned one has to be unpinned first.
        if (iommu_enabled) {
            tt_unpin_pages(tt_device_handle, dma_buffer.buffer, dma_buffer.size + page_size());
        }
        munmap(dma_buffer.buffer, dma_buffer.size + page_size());
        dma_buffer = DmaBuffer{};
    }
    if (tlb_config_space != nullptr) {
        munmap(tlb_config_space, tlb_config_space_size);
        tlb_config_space = nullptr;
    }
    if (bar0 != nullptr) {
        munmap(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(bar0) + bar0_mapping_offset), bar0_size);
        bar0 = nullptr;
    }
    if (pci_device_file_desc >= 0) {
        close(pci_device_file_desc);
        pci_device_file_desc = -1;
    }
    if (tt_device_handle != nullptr) {
        tt_device_close(tt_device_handle);
        tt_device_handle = nullptr;
    }
}

void PCIDevice::allocate_pcie_dma_buffer() {
    for (size_t size = MAX_PCIE_DMA_BUFFER_SIZE; size >= static_cast<size_t>(page_size()); size /= 2) {
        const bool allocated =
            iommu_enabled ? try_allocate_pcie_dma_buffer_iommu(size) : try_allocate_pcie_dma_buffer_no_iommu(size);
        if (allocated) {
            log_debug(LogUMD, "Allocated a {} byte PCIe DMA buffer for {}.", size, device_path);
            return;
        }
    }
    log_warning(LogUMD, "Could not allocate a PCIe DMA buffer for {}; PCIe DMA will not be available.", device_path);
}

// Both variants place a one page completion area right behind the buffer, inside the same mapping, so a single
// contiguous device address range covers both.
bool PCIDevice::try_allocate_pcie_dma_buffer_iommu(const size_t dma_buf_size) {
    const size_t mapping_size = dma_buf_size + page_size();
    void *mapping =
        mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (mapping == MAP_FAILED) {
        return false;
    }
    uint64_t iova = 0;
    if (tt_pin_pages(tt_device_handle, mapping, mapping_size, TT_DMA_FLAG_NONE, &iova, nullptr) != 0) {
        munmap(mapping, mapping_size);
        return false;
    }
    dma_buffer.buffer = static_cast<uint8_t *>(mapping);
    dma_buffer.completion = dma_buffer.buffer + dma_buf_size;
    dma_buffer.size = dma_buf_size;
    dma_buffer.buffer_pa = iova;
    dma_buffer.completion_pa = iova + dma_buf_size;
    return true;
}

bool PCIDevice::try_allocate_pcie_dma_buffer_no_iommu(const size_t dma_buf_size) {
    const size_t mapping_size = dma_buf_size + page_size();
    void *mapping = nullptr;
    uint64_t physical_address = 0;
    if (tt_allocate_dma_buf(
            tt_device_handle, 0, mapping_size, TT_DMA_FLAG_NONE, &mapping, &physical_address, nullptr) != 0) {
        return false;
    }
    dma_buffer.buffer = static_cast<uint8_t *>(mapping);
    dma_buffer.completion = dma_buffer.buffer + dma_buf_size;
    dma_buffer.size = dma_buf_size;
    dma_buffer.buffer_pa = physical_address;
    dma_buffer.completion_pa = physical_address + dma_buf_size;
    return true;
}

std::optional<uint32_t> PCIDevice::get_current_link_width() const {
    const std::optional<long> width = read_sysfs_integer(info, "current_link_width");
    if (!width.has_value() || *width <= 0) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*width);
}

uint64_t PCIDevice::map_for_hugepage(void *buffer, size_t size) {
    uint64_t physical_address = 0;
    const int result = tt_pin_pages(tt_device_handle, buffer, size, TT_DMA_FLAG_CONTIGUOUS, &physical_address, nullptr);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to pin a {} byte hugepage on {} (error {}).", size, device_path, result));
    }
    return physical_address;
}

std::pair<uint64_t, uint64_t> PCIDevice::map_buffer_to_noc(
    void *buffer, size_t size, DeviceBufferAccess device_access) {
    if (device_access == DeviceBufferAccess::READ_ONLY && !is_read_only_page_pinning_supported()) {
        UMD_THROW(error::RuntimeError, fmt::format("{} cannot map buffers device-read-only.", device_path));
    }
    uint64_t dma_address = 0;
    uint64_t noc_address = 0;
    const int result = tt_pin_pages(
        tt_device_handle, buffer, size, dma_map_flags(device_access, TT_DMA_FLAG_NOC), &dma_address, &noc_address);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to map a {} byte buffer to the NOC of {} (error {}).", size, device_path, result));
    }
    return {noc_address, dma_address};
}

std::pair<uint64_t, uint64_t> PCIDevice::map_hugepage_to_noc(void *hugepage, size_t size) {
    uint64_t physical_address = 0;
    uint64_t noc_address = 0;
    const int result = tt_pin_pages(
        tt_device_handle, hugepage, size, TT_DMA_FLAG_CONTIGUOUS | TT_DMA_FLAG_NOC, &physical_address, &noc_address);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to map a {} byte hugepage to the NOC of {} (error {}).", size, device_path, result));
    }
    return {noc_address, physical_address};
}

uint64_t PCIDevice::map_for_dma(void *buffer, size_t size, DeviceBufferAccess device_access) {
    if (device_access == DeviceBufferAccess::READ_ONLY && !is_read_only_page_pinning_supported()) {
        UMD_THROW(error::RuntimeError, fmt::format("{} cannot map buffers device-read-only.", device_path));
    }
    // Without an IOMMU the device sees physical addresses, so only a physically contiguous buffer (a hugepage)
    // can be handed to it as one range; the caller attests to that by mapping such a buffer.
    const int flags = dma_map_flags(device_access, iommu_enabled ? TT_DMA_FLAG_NONE : TT_DMA_FLAG_CONTIGUOUS);
    uint64_t dma_address = 0;
    const int result = tt_pin_pages(tt_device_handle, buffer, size, flags, &dma_address, nullptr);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to map a {} byte buffer for DMA on {} (error {}).", size, device_path, result));
    }
    return dma_address;
}

bool PCIDevice::is_read_only_page_pinning_supported() const {
    return iommu_enabled && kmd_version >= KMD_READ_ONLY_PAGE_PINNING;
}

void PCIDevice::unmap_for_dma(void *buffer, size_t size) {
    const int result = tt_unpin_pages(tt_device_handle, buffer, size);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to unmap a {} byte buffer from {} (error {}).", size, device_path, result));
    }
}

SemVer PCIDevice::read_kmd_version() {
    // Without the driver loaded there is no version to report; 0.0.0 fails every version gate.
    const std::optional<std::string> version = read_first_token(KMD_VERSION_PATH);
    if (!version.has_value()) {
        log_debug(LogUMD, "{} is not readable; the KMD is not loaded.", KMD_VERSION_PATH);
        return SemVer();
    }
    return SemVer(*version);
}

SemVer PCIDevice::read_kernel_version() {
    utsname name{};
    if (uname(&name) != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("uname() failed: {}.", strerror(errno)));
    }
    uint64_t major = 0;
    uint64_t minor = 0;
    uint64_t patch = 0;
    if (std::sscanf(name.release, "%lu.%lu.%lu", &major, &minor, &patch) < 2) {
        UMD_THROW(error::RuntimeError, fmt::format("Cannot parse kernel release {}.", name.release));
    }
    return SemVer(major, minor, patch);
}

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(
    const size_t tlb_size, const TlbMapping tlb_mapping, const bool verify_config) {
    return std::make_unique<SiliconTlbHandle>(*this, tlb_size, tlb_mapping, verify_config);
}

void PCIDevice::configure_tlb(const uint32_t tlb_index, const tlb_data &tlb_config, const bool verify) {
    if (tlb_config_space == nullptr) {
        UMD_THROW(error::RuntimeError, fmt::format("{} has no TLB configuration space mapped.", device_path));
    }
    const ArchitectureTlbs &tlbs = get_architecture_tlbs(arch);
    const tlb_configuration configuration = tlbs.get_configuration(tlb_index);
    if (tlb_config.local_offset % configuration.size != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "TLB {} cannot be pointed at {:#x}: a window's address has to be aligned to its size {:#x}.",
                tlb_index,
                tlb_config.local_offset,
                configuration.size));
    }

    // The register holds the window's address in units of the window size.
    tlb_data register_config = tlb_config;
    register_config.local_offset = tlb_config.local_offset / configuration.size;
    if (!register_config.check(configuration.offset)) {
        UMD_THROW(error::RuntimeError, fmt::format("TLB {} configuration does not fit its register.", tlb_index));
    }
    const auto [low, high] = register_config.apply_offset(configuration.offset);
    const uint32_t words[4] = {
        static_cast<uint32_t>(low),
        static_cast<uint32_t>(low >> 32),
        static_cast<uint32_t>(high),
        static_cast<uint32_t>(high >> 32)};
    const size_t word_count = tlbs.cfg_reg_size_bytes / sizeof(uint32_t);

    const uint64_t register_offset =
        configuration.cfg_addr - tlbs.static_cfg_addr + configuration.index_offset * tlbs.cfg_reg_size_bytes;
    volatile uint32_t *reg =
        reinterpret_cast<volatile uint32_t *>(static_cast<uint8_t *>(tlb_config_space) + register_offset);

    // Stores still sitting in write-combining buffers would otherwise reach the window after it has moved.
    tt_driver_atomics::sfence();
    for (size_t i = 0; i < word_count; i++) {
        reg[i] = words[i];
    }

    if (!verify) {
        return;
    }
    // A read cannot pass the posted writes before it, so once the register reads back the new value the
    // configuration is live for every requester.
    const auto deadline = std::chrono::steady_clock::now() + TLB_CONFIG_VERIFY_TIMEOUT;
    while (true) {
        bool matches = true;
        for (size_t i = 0; i < word_count; i++) {
            matches &= reg[i] == words[i];
        }
        if (matches) {
            return;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            UMD_THROW(error::RuntimeError, fmt::format("TLB {} configuration did not take effect.", tlb_index));
        }
    }
}

uint8_t PCIDevice::read_command_byte(const int pci_device_num) {
    // The command register is at offset 4 of the PCI configuration header. Reading it through sysfs works while
    // the device is mid-reset, when the device node cannot serve requests.
    constexpr std::streamoff COMMAND_REGISTER_OFFSET = 4;
    const std::string config_path = fmt::format("/sys/class/tenstorrent/tenstorrent!{}/device/config", pci_device_num);
    std::ifstream config(config_path, std::ios::binary);
    char command = 0;
    if (!config.seekg(COMMAND_REGISTER_OFFSET) || !config.read(&command, 1)) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to read the PCI command register from {}.", config_path));
    }
    return static_cast<uint8_t>(command);
}

void PCIDevice::send_reset_ioctl_to_devices(
    const std::unordered_set<int> &pci_target_devices, TenstorrentResetDevice flag, bool ignore_failures) {
    for (const int pci_device_num : pci_target_devices) {
        const std::string path = device_path_for(pci_device_num);
        tt_device_t *handle = nullptr;
        int result = tt_device_open(path.c_str(), &handle, O_APPEND);
        if (result == 0) {
            result = tt_device_reset(handle, static_cast<uint32_t>(flag));
            tt_device_close(handle);
        }
        if (result == 0) {
            continue;
        }
        const std::string message =
            fmt::format("Reset ioctl {} failed on {} (error {}).", static_cast<uint32_t>(flag), path, result);
        if (!ignore_failures) {
            UMD_THROW(error::RuntimeError, message);
        }
        log_warning(LogUMD, "{}", message);
    }
}

tt::ARCH PCIDevice::get_pcie_arch() {
    static const tt::ARCH pcie_arch = [] {
        const std::map<int, PciDeviceInfo> devices = enumerate_devices_info();
        return devices.empty() ? tt::ARCH::Invalid : devices.begin()->second.get_arch();
    }();
    return pcie_arch;
}

bool PCIDevice::is_arch_agnostic_reset_supported() { return read_kmd_version() >= KMD_ARCH_AGNOSTIC_RESET; }

bool PCIDevice::is_tlb_dmabuf_export_supported() {
    return read_kmd_version() >= KMD_TLB_DMABUF_EXPORT && read_kernel_version() >= MIN_KERNEL_TLB_DMABUF_EXPORT;
}

int PCIDevice::export_tlb_dmabuf(size_t window_size, const tlb_data &config, uint64_t offset, uint64_t size) {
    if (size == 0 || !is_page_aligned(offset) || !is_page_aligned(size) || offset > window_size ||
        size > window_size - offset) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Cannot export [{:#x}, +{:#x}) of a {:#x} byte TLB window: the range has to be non-empty, page "
                "aligned and inside the window.",
                offset,
                size,
                window_size));
    }
    if (config.local_offset % window_size != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Cannot export a TLB window at {:#x}: it has to be aligned to the window size {:#x}.",
                config.local_offset,
                window_size));
    }
    if (!is_tlb_dmabuf_export_supported()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Exporting TLB windows needs KMD {} and Linux {}; this system has KMD {} and Linux {}.",
                KMD_TLB_DMABUF_EXPORT.str(),
                MIN_KERNEL_TLB_DMABUF_EXPORT.str(),
                read_kmd_version().str(),
                read_kernel_version().str()));
    }

    // The window is set up through the driver rather than the configuration space, because the driver is what an
    // importer's traffic is routed by. Once exported, the dma-buf pins the window, so it is released right away.
    tt_tlb_t *tlb = nullptr;
    int result = tt_tlb_alloc(tt_device_handle, window_size, TT_MMIO_CACHE_MODE_UC, &tlb);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to allocate a {:#x} byte TLB window on {} (error {}).", window_size, device_path, result));
    }
    tt_noc_addr_config_t kmd_config = to_kmd_noc_config(config);
    int dmabuf_fd = -1;
    result = tt_tlb_map(tt_device_handle, tlb, &kmd_config);
    if (result == 0) {
        result = tt_tlb_export_dmabuf(tt_device_handle, tlb, offset, size, &dmabuf_fd);
    }
    tt_tlb_free(tt_device_handle, tlb);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to export a TLB window of {} as a dma-buf (error {}).", device_path, result));
    }
    return dmabuf_fd;
}

uint64_t PCIDevice::noc_read_scalar(uint64_t addr, uint32_t width, NocAddressSpace space) {
    const uint32_t flags = space == NocAddressSpace::DeviceLocal ? TT_NOC_FLAG_KLA : TT_NOC_FLAG_NONE;
    uint64_t value = 0;
    const int result = tt_noc_read_scalar(tt_device_handle, addr, &value, width, flags);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "The driver refused a {} byte read at {:#x} on {} (error {}).", width, addr, device_path, result));
    }
    return value;
}

void PCIDevice::noc_write_scalar(uint64_t addr, uint64_t value, uint32_t width, NocAddressSpace space) {
    const uint32_t flags = space == NocAddressSpace::DeviceLocal ? TT_NOC_FLAG_KLA : TT_NOC_FLAG_NONE;
    const int result = tt_noc_write_scalar(tt_device_handle, addr, value, width, flags);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "The driver refused a {} byte write at {:#x} on {} (error {}).", width, addr, device_path, result));
    }
}

void PCIDevice::set_power_state(bool busy) {
    // Only Blackhole has power domains the driver manages, and only since the KMD grew the power API.
    if (arch != tt::ARCH::BLACKHOLE || kmd_version < KMD_POWER_STATE) {
        return;
    }
    const uint16_t power_flags = busy ? (TT_POWER_FLAG_MAX_AI_CLK | TT_POWER_FLAG_MRISC_PHY_WAKEUP |
                                         TT_POWER_FLAG_TENSIX_ENABLE | TT_POWER_FLAG_L2CPU_ENABLE)
                                      : 0;
    const int result = tt_device_set_power_state(tt_device_handle, power_flags);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Failed to set the power state of {} (error {}).", device_path, result));
    }
}

}  // namespace tt::umd
