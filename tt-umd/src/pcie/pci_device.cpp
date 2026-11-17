// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/pci_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
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

// Selects the devices TT_VISIBLE_DEVICES names among all_devices, which are in BDF order. A token is either an
// index into that order or a BDF fragment matching every device whose BDF contains it.
std::vector<int> filter_visible_devices(
    const std::vector<int> &all_devices, const std::map<int, PciDeviceInfo> &all_infos, const std::string &value) {
    std::set<int> selected;
    for (const std::string &token : utils::split_string_by_comma(value)) {
        if (utils::is_integer_string(token)) {
            const size_t index = std::stoul(token);
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
            bool matched = false;
            for (const auto &[device_num, info] : all_infos) {
                if (info.pci_bdf.find(token) != std::string::npos) {
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
    std::vector<int> visible;
    std::copy_if(all_devices.begin(), all_devices.end(), std::back_inserter(visible), [&](int device_num) {
        return selected.count(device_num) != 0;
    });
    return visible;
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

std::vector<int> PCIDevice::get_all_device_ids() {
    std::vector<int> device_ids;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(DEVICE_DIRECTORY, ec)) {
        const std::string name = entry.path().filename().string();
        if (utils::is_integer_string(name)) {
            device_ids.push_back(std::stoi(name));
        }
    }
    return sort_ids_based_on_bdf(device_ids);
}

std::map<std::string, int> PCIDevice::get_bdf_to_device_id_map() {
    std::map<std::string, int> bdf_to_device_id;
    for (const int device_num : get_all_device_ids()) {
        bdf_to_device_id.emplace(read_device_info(device_path_for(device_num)).pci_bdf, device_num);
    }
    return bdf_to_device_id;
}

std::vector<int> PCIDevice::sort_ids_based_on_bdf(const std::vector<int> &pci_device_ids) {
    std::vector<std::pair<std::string, int>> bdf_and_id;
    bdf_and_id.reserve(pci_device_ids.size());
    for (const int device_num : pci_device_ids) {
        bdf_and_id.emplace_back(read_device_info(device_path_for(device_num)).pci_bdf, device_num);
    }
    std::sort(bdf_and_id.begin(), bdf_and_id.end());
    std::vector<int> sorted_ids;
    sorted_ids.reserve(bdf_and_id.size());
    for (const auto &[bdf, device_num] : bdf_and_id) {
        sorted_ids.push_back(device_num);
    }
    return sorted_ids;
}

std::vector<int> PCIDevice::enumerate_devices() {
    const std::vector<int> all_devices = get_all_device_ids();
    const std::optional<std::string> visible_devices = utils::get_env_var_value(utils::TT_VISIBLE_DEVICES_ENV.data());
    if (!visible_devices.has_value()) {
        return all_devices;
    }
    std::map<int, PciDeviceInfo> all_infos;
    for (const int device_num : all_devices) {
        all_infos.emplace(device_num, read_device_info(device_path_for(device_num)));
    }
    std::vector<int> visible = filter_visible_devices(all_devices, all_infos, visible_devices.value());
    log_debug(
        LogUMD,
        "{}={} leaves {} of {} devices.",
        utils::TT_VISIBLE_DEVICES_ENV,
        *visible_devices,
        visible.size(),
        all_devices.size());
    return visible;
}

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    std::map<int, PciDeviceInfo> infos;
    for (const int device_num : enumerate_devices()) {
        infos.emplace(device_num, read_device_info(device_path_for(device_num)));
    }
    return infos;
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
