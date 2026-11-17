// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/pci_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
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

#include "common/unsupported.hpp"
#include "common/utils.hpp"
#include "tt-kmd-lib/pci_ids.h"
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd-utils/internal/host.hpp"
#include "tt-umd-utils/internal/string_utils.hpp"
#include "tt-umd/utils/error.hpp"

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

bool PCIDevice::detect_iommu(const PciDeviceInfo &device_info) { UMD_NOT_IMPLEMENTED(); }

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(device_path_for(pci_device_number)),
    pci_device_num(pci_device_number),
    info(),
    numa_node(-1),
    revision(0),
    arch(tt::ARCH::Invalid),
    kmd_version(),
    iommu_enabled(false) {
    UMD_NOT_IMPLEMENTED();
}

PCIDevice::~PCIDevice() = default;

std::optional<uint32_t> PCIDevice::get_current_link_width() const { UMD_NOT_IMPLEMENTED(); }

uint64_t PCIDevice::map_for_hugepage(void *buffer, size_t size) { UMD_NOT_IMPLEMENTED(); }

std::pair<uint64_t, uint64_t> PCIDevice::map_buffer_to_noc(
    void *buffer, size_t size, DeviceBufferAccess device_access) {
    UMD_NOT_IMPLEMENTED();
}

std::pair<uint64_t, uint64_t> PCIDevice::map_hugepage_to_noc(void *hugepage, size_t size) { UMD_NOT_IMPLEMENTED(); }

uint64_t PCIDevice::map_for_dma(void *buffer, size_t size, DeviceBufferAccess device_access) { UMD_NOT_IMPLEMENTED(); }

bool PCIDevice::is_read_only_page_pinning_supported() const { UMD_NOT_IMPLEMENTED(); }

void PCIDevice::unmap_for_dma(void *buffer, size_t size) { UMD_NOT_IMPLEMENTED(); }

SemVer PCIDevice::read_kmd_version() { UMD_NOT_IMPLEMENTED(); }

SemVer PCIDevice::read_kernel_version() { UMD_NOT_IMPLEMENTED(); }

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(
    const size_t tlb_size, const TlbMapping tlb_mapping, const bool verify_config) {
    UMD_NOT_IMPLEMENTED();
}

void PCIDevice::configure_tlb(const uint32_t tlb_index, const tlb_data &tlb_config, const bool verify) {
    UMD_NOT_IMPLEMENTED();
}

uint8_t PCIDevice::read_command_byte(const int pci_device_num) { UMD_NOT_IMPLEMENTED(); }

void PCIDevice::send_reset_ioctl_to_devices(
    const std::unordered_set<int> &pci_target_devices, TenstorrentResetDevice flag, bool ignore_failures) {
    UMD_NOT_IMPLEMENTED();
}

tt::ARCH PCIDevice::get_pcie_arch() { UMD_NOT_IMPLEMENTED(); }

bool PCIDevice::is_arch_agnostic_reset_supported() { UMD_NOT_IMPLEMENTED(); }

bool PCIDevice::is_tlb_dmabuf_export_supported() { UMD_NOT_IMPLEMENTED(); }

int PCIDevice::export_tlb_dmabuf(size_t window_size, const tlb_data &config, uint64_t offset, uint64_t size) {
    UMD_NOT_IMPLEMENTED();
}

uint64_t PCIDevice::noc_read_scalar(uint64_t addr, uint32_t width, NocAddressSpace space) { UMD_NOT_IMPLEMENTED(); }

void PCIDevice::noc_write_scalar(uint64_t addr, uint64_t value, uint32_t width, NocAddressSpace space) {
    UMD_NOT_IMPLEMENTED();
}

void PCIDevice::set_power_state(bool busy) { UMD_NOT_IMPLEMENTED(); }

void PCIDevice::release_resources() { UMD_NOT_IMPLEMENTED(); }

void PCIDevice::construct_resources() { UMD_NOT_IMPLEMENTED(); }

void PCIDevice::allocate_pcie_dma_buffer() { UMD_NOT_IMPLEMENTED(); }

bool PCIDevice::try_allocate_pcie_dma_buffer_iommu(const size_t dma_buf_size) { UMD_NOT_IMPLEMENTED(); }

bool PCIDevice::try_allocate_pcie_dma_buffer_no_iommu(const size_t dma_buf_size) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
