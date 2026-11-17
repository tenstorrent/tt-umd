// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/pci_device.hpp"

#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <tt-kmd-lib/pci_ids.h>
#include <tt-kmd-lib/tt_kmd_lib.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "common/utils.hpp"
#include "tt-umd/arch/architecture_registers.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/silicon_tlb_handle.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/kmd_versions.hpp"

namespace tt::umd {

namespace {

namespace fs = std::filesystem;

constexpr const char *DEVICE_NODE_DIR = "/dev/tenstorrent";

// O_APPEND makes the fd power-aware (KMD >= 2.6): it votes for nothing until set_power_state() is
// called, so an open device can idle. Older KMDs ignore the flag.
constexpr int DEVICE_OPEN_FLAGS = O_RDWR | O_CLOEXEC | O_APPEND;

// The largest bounce buffer allocate_pcie_dma_buffer() tries; it halves from here down to one page.
constexpr size_t MAX_PCIE_DMA_BUFFER_SIZE = 16 * (1 << 20);

// Slot of the driver-allocated DMA buffer on this device's fd, used when there is no IOMMU.
constexpr uint8_t PCIE_DMA_BUFFER_INDEX = 0;

// How long configure_tlb(verify = true) waits for the written configuration to read back.
constexpr auto TLB_CONFIG_VERIFY_TIMEOUT = std::chrono::seconds(1);

std::string sysfs_class_device_dir(int pci_device_num) {
    return fmt::format("/sys/class/tenstorrent/tenstorrent!{}/device", pci_device_num);
}

std::string sysfs_pci_device_dir(const std::string &pci_bdf) { return "/sys/bus/pci/devices/" + pci_bdf; }

std::optional<std::string> read_sysfs_string(const std::string &path) {
    std::ifstream file(path);
    std::string value;
    if (!file || !std::getline(file, value)) {
        return std::nullopt;
    }
    value.erase(value.find_last_not_of(" \n\r\t") + 1);
    return value;
}

// Parses a sysfs number; base 0 accepts both the decimal and the 0x-prefixed hex attributes.
std::optional<int64_t> read_sysfs_number(const std::string &path) {
    const std::optional<std::string> value = read_sysfs_string(path);
    if (!value.has_value()) {
        return std::nullopt;
    }
    try {
        return std::stoll(*value, nullptr, 0);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

uint16_t read_sysfs_id(const std::string &device_dir, const char *attribute) {
    const std::optional<int64_t> value = read_sysfs_number(device_dir + "/" + attribute);
    if (!value.has_value()) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to read {}/{}.", device_dir, attribute));
    }
    return static_cast<uint16_t>(*value);
}

int device_num_from_path(const std::string &device_path) {
    const std::string number = fs::path(device_path).filename().string();
    if (!utils::is_integer_string(number)) {
        UMD_THROW(error::RuntimeError, fmt::format("{} is not a /dev/tenstorrent/N device path.", device_path));
    }
    return std::stoi(number);
}

// PCI BDF of /dev/tenstorrent/N, read from the PCI device its sysfs class entry links to.
std::optional<std::string> read_pci_bdf(int pci_device_num) {
    std::error_code error;
    const fs::path pci_dir = fs::canonical(sysfs_class_device_dir(pci_device_num), error);
    if (error) {
        return std::nullopt;
    }
    return pci_dir.filename().string();
}

// Slots are listed by bus address without the function, e.g. "0000:01:00".
std::optional<int> find_physical_slot(const std::string &pci_bdf) {
    const std::string slot_address = pci_bdf.substr(0, pci_bdf.rfind('.'));
    std::error_code error;
    for (const fs::directory_entry &slot : fs::directory_iterator("/sys/bus/pci/slots", error)) {
        if (read_sysfs_string(slot.path().string() + "/address") == slot_address &&
            utils::is_integer_string(slot.path().filename().string())) {
            return std::stoi(slot.path().filename().string());
        }
    }
    return std::nullopt;
}

void check_kmd_result(int result, const std::string &operation) {
    if (result != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("{} failed: {}", operation, std::strerror(-result)));
    }
}

void *mmap_device(int fd, uint64_t offset, size_t size, const std::string &what) {
    void *mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, static_cast<off_t>(offset));
    if (mapping == MAP_FAILED) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to map {}: {}", what, std::strerror(errno)));
    }
    return mapping;
}

void munmap_if_mapped(void *&mapping, size_t size) {
    if (mapping != nullptr) {
        munmap(mapping, size);
        mapping = nullptr;
    }
}

int to_kmd_dma_flags(DeviceBufferAccess device_access) {
    return device_access == DeviceBufferAccess::READ_ONLY ? TT_DMA_FLAG_READ_ONLY : TT_DMA_FLAG_NONE;
}

size_t page_size() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }

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

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(fmt::format("{}/{}", DEVICE_NODE_DIR, pci_device_number)),
    pci_device_num(pci_device_number),
    info(read_device_info(device_path)),
    numa_node(static_cast<int>(read_sysfs_number(sysfs_pci_device_dir(info.pci_bdf) + "/numa_node").value_or(-1))),
    revision(static_cast<int>(read_sysfs_number(sysfs_pci_device_dir(info.pci_bdf) + "/revision").value_or(0))),
    arch(info.get_arch()),
    kmd_version(read_kmd_version()),
    iommu_enabled(detect_iommu(info)) {
    if (kmd_version < KMD_MINIMUM_VERSION) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "KMD version {} is older than the minimum supported {}.",
                kmd_version.to_string(),
                KMD_MINIMUM_VERSION.to_string()));
    }
    try {
        construct_resources();
    } catch (...) {
        release_resources();
        throw;
    }
    log_debug(
        tt::LogUMD,
        "Opened {} ({}, BDF {}, NUMA node {}, IOMMU {}).",
        device_path,
        arch_to_str(arch),
        info.pci_bdf,
        numa_node,
        iommu_enabled ? "on" : "off");
}

PCIDevice::~PCIDevice() { release_resources(); }

void PCIDevice::construct_resources() {
    bar2_uc_size = 0;
    read_checking_offset = 0;
    // BARs are mapped through a plain fd, since the tt-kmd-lib handle does not expose its own.
    pci_device_file_desc = open(device_path.c_str(), DEVICE_OPEN_FLAGS);
    if (pci_device_file_desc < 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to open {}: {}", device_path, std::strerror(errno)));
    }
    check_kmd_result(
        tt_device_open(device_path.c_str(), &tt_device_handle, O_APPEND),
        fmt::format("tt_device_open({})", device_path));

    tt_bar_mappings_t mappings{};
    check_kmd_result(tt_device_query_bar_mappings(tt_device_handle, &mappings), "Querying BAR mappings");
    if (mappings.resource0_uc.id == TT_BAR_MAPPING_UNUSED ||
        mappings.resource0_uc.size < bar0_mapping_offset + bar0_size) {
        UMD_THROW(error::RuntimeError, fmt::format("{} does not expose a usable BAR0.", device_path));
    }

    bar0 = mmap_device(
        pci_device_file_desc, mappings.resource0_uc.base + bar0_mapping_offset, bar0_size, "BAR0 register space");

    if (arch != tt::ARCH::Invalid) {
        tlb_config_space = mmap_device(
            pci_device_file_desc,
            mappings.resource0_uc.base + get_architecture_tlbs(arch).static_cfg_addr,
            tlb_config_space_size,
            "TLB configuration registers");
        read_checking_offset = get_architecture_registers(arch).noc_node_id_bar_offset;
    }

    if (mappings.resource1_uc.id != TT_BAR_MAPPING_UNUSED) {
        bar2_uc_size = mappings.resource1_uc.size;
        bar2_uc = mmap_device(pci_device_file_desc, mappings.resource1_uc.base, bar2_uc_size, "BAR2");
    }

    allocate_pcie_dma_buffer();
}

void PCIDevice::release_resources() {
    if (dma_buffer.buffer != nullptr) {
        const size_t mapping_size = dma_buffer.size + page_size();
        if (iommu_enabled && tt_device_handle != nullptr) {
            tt_unpin_pages(tt_device_handle, dma_buffer.buffer, mapping_size);
        }
        munmap(dma_buffer.buffer, mapping_size);
        dma_buffer = DmaBuffer{};
    }
    munmap_if_mapped(bar0, bar0_size);
    munmap_if_mapped(tlb_config_space, tlb_config_space_size);
    munmap_if_mapped(bar2_uc, bar2_uc_size);
    if (tt_device_handle != nullptr) {
        tt_device_close(tt_device_handle);
        tt_device_handle = nullptr;
    }
    if (pci_device_file_desc >= 0) {
        close(pci_device_file_desc);
        pci_device_file_desc = -1;
    }
}

std::vector<int> PCIDevice::get_all_device_ids() {
    std::vector<int> device_ids;
    std::error_code error;
    for (const fs::directory_entry &entry : fs::directory_iterator(DEVICE_NODE_DIR, error)) {
        const std::string name = entry.path().filename().string();
        if (utils::is_integer_string(name)) {
            device_ids.push_back(std::stoi(name));
        }
    }
    return device_ids;
}

std::map<std::string, int> PCIDevice::get_bdf_to_device_id_map() {
    std::map<std::string, int> bdf_to_device_id;
    for (int device_id : get_all_device_ids()) {
        if (std::optional<std::string> bdf = read_pci_bdf(device_id)) {
            bdf_to_device_id.emplace(*bdf, device_id);
        }
    }
    return bdf_to_device_id;
}

std::vector<int> PCIDevice::sort_ids_based_on_bdf(const std::vector<int> &pci_device_ids) {
    std::vector<int> sorted_ids;
    // BDFs are fixed-width lower-case hex, so the map's string order is bus order.
    for (const auto &[bdf, device_id] : get_bdf_to_device_id_map()) {
        if (std::find(pci_device_ids.begin(), pci_device_ids.end(), device_id) != pci_device_ids.end()) {
            sorted_ids.push_back(device_id);
        }
    }
    for (int device_id : pci_device_ids) {
        if (std::find(sorted_ids.begin(), sorted_ids.end(), device_id) == sorted_ids.end()) {
            sorted_ids.push_back(device_id);
        }
    }
    return sorted_ids;
}

std::vector<int> PCIDevice::enumerate_devices() {
    const std::vector<int> all_ids = sort_ids_based_on_bdf(get_all_device_ids());

    const std::optional<std::string> visible_devices = utils::get_env_var_value(utils::TT_VISIBLE_DEVICES_ENV.data());
    if (!visible_devices.has_value() || visible_devices->empty()) {
        return all_ids;
    }

    // An integer token is a logical id, i.e. an index into the BDF-sorted device list; any other token
    // is a BDF, or a BDF prefix selecting every device under it.
    std::set<size_t> visible_indices;
    for (const std::string &token : utils::split_string_by_comma(*visible_devices)) {
        if (utils::is_integer_string(token)) {
            const size_t index = std::stoul(token);
            if (index >= all_ids.size()) {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format(
                        "Invalid device id {} in {}: only {} devices are present.",
                        token,
                        utils::TT_VISIBLE_DEVICES_ENV,
                        all_ids.size()));
            }
            visible_indices.insert(index);
        } else if (utils::is_bdf_string(token)) {
            bool matched = false;
            for (size_t index = 0; index < all_ids.size(); index++) {
                const std::optional<std::string> bdf = read_pci_bdf(all_ids[index]);
                if (bdf.has_value() && bdf->rfind(token, 0) == 0) {
                    visible_indices.insert(index);
                    matched = true;
                }
            }
            if (!matched) {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("No device matches BDF {} in {}.", token, utils::TT_VISIBLE_DEVICES_ENV));
            }
        } else {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Invalid device identifier {} in {}: expected a device index or a PCI BDF.",
                    token,
                    utils::TT_VISIBLE_DEVICES_ENV));
        }
    }

    std::vector<int> visible_ids;
    for (size_t index : visible_indices) {
        visible_ids.push_back(all_ids[index]);
    }
    return visible_ids;
}

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    std::map<int, PciDeviceInfo> devices_info;
    for (int device_id : enumerate_devices()) {
        devices_info.emplace(device_id, read_device_info(fmt::format("{}/{}", DEVICE_NODE_DIR, device_id)));
    }
    return devices_info;
}

std::optional<int> PCIDevice::get_pci_device_id(int umd_logical_id) {
    const std::vector<int> device_ids = enumerate_devices();
    if (umd_logical_id < 0 || static_cast<size_t>(umd_logical_id) >= device_ids.size()) {
        return std::nullopt;
    }
    return device_ids[umd_logical_id];
}

PciDeviceInfo PCIDevice::read_device_info(const std::string &device_path) {
    const int pci_device_num = device_num_from_path(device_path);
    const std::optional<std::string> bdf = read_pci_bdf(pci_device_num);
    if (!bdf.has_value()) {
        UMD_THROW(error::RuntimeError, fmt::format("No PCI device found in sysfs for {}.", device_path));
    }

    PciDeviceInfo info{};
    const std::string pci_dir = sysfs_pci_device_dir(*bdf);
    info.vendor_id = read_sysfs_id(pci_dir, "vendor");
    info.device_id = read_sysfs_id(pci_dir, "device");
    info.subsystem_vendor_id = read_sysfs_id(pci_dir, "subsystem_vendor");
    info.subsystem_id = read_sysfs_id(pci_dir, "subsystem_device");

    unsigned int domain = 0;
    unsigned int bus = 0;
    unsigned int device = 0;
    unsigned int function = 0;
    if (std::sscanf(bdf->c_str(), "%x:%x:%x.%x", &domain, &bus, &device, &function) != 4) {
        UMD_THROW(error::RuntimeError, fmt::format("Cannot parse PCI BDF {} of {}.", *bdf, device_path));
    }
    info.pci_domain = static_cast<uint16_t>(domain);
    info.pci_bus = static_cast<uint16_t>(bus);
    info.pci_device = static_cast<uint16_t>(device);
    info.pci_function = static_cast<uint16_t>(function);
    info.pci_bdf = *bdf;
    info.physical_slot = find_physical_slot(*bdf);
    return info;
}

bool PCIDevice::detect_iommu(const PciDeviceInfo &device_info) {
    // The KMD treats a device as translated when its IOMMU domain is not an identity (passthrough)
    // one; sysfs names translating domains "DMA" or "DMA-FQ".
    const std::optional<std::string> domain_type =
        read_sysfs_string(sysfs_pci_device_dir(device_info.pci_bdf) + "/iommu_group/type");
    return domain_type.has_value() && domain_type->rfind("DMA", 0) == 0;
}

std::optional<uint32_t> PCIDevice::get_current_link_width() const {
    const std::optional<int64_t> width = read_sysfs_number(sysfs_pci_device_dir(info.pci_bdf) + "/current_link_width");
    if (!width.has_value() || *width <= 0) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*width);
}

uint64_t PCIDevice::map_for_hugepage(void *buffer, size_t size) {
    uint64_t physical_address = 0;
    check_kmd_result(
        tt_pin_pages(tt_device_handle, buffer, size, TT_DMA_FLAG_CONTIGUOUS, &physical_address, nullptr),
        fmt::format("Pinning hugepage of {} bytes on {}", size, device_path));
    return physical_address;
}

std::pair<uint64_t, uint64_t> PCIDevice::map_buffer_to_noc(
    void *buffer, size_t size, DeviceBufferAccess device_access) {
    if (device_access == DeviceBufferAccess::READ_ONLY && !is_read_only_page_pinning_supported()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Read-only pinning needs an IOMMU and KMD {}.", KMD_READ_ONLY_PAGE_PINNING.to_string()));
    }
    uint64_t dma_address = 0;
    uint64_t noc_address = 0;
    check_kmd_result(
        tt_pin_pages(
            tt_device_handle,
            buffer,
            size,
            TT_DMA_FLAG_NOC | to_kmd_dma_flags(device_access),
            &dma_address,
            &noc_address),
        fmt::format("Mapping {} bytes to the NOC of {}", size, device_path));
    return {noc_address, dma_address};
}

std::pair<uint64_t, uint64_t> PCIDevice::map_hugepage_to_noc(void *hugepage, size_t size) {
    uint64_t dma_address = 0;
    uint64_t noc_address = 0;
    check_kmd_result(
        tt_pin_pages(
            tt_device_handle, hugepage, size, TT_DMA_FLAG_NOC | TT_DMA_FLAG_CONTIGUOUS, &dma_address, &noc_address),
        fmt::format("Mapping hugepage of {} bytes to the NOC of {}", size, device_path));
    return {noc_address, dma_address};
}

uint64_t PCIDevice::map_for_dma(void *buffer, size_t size, DeviceBufferAccess device_access) {
    if (device_access == DeviceBufferAccess::READ_ONLY && !is_read_only_page_pinning_supported()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Read-only pinning needs an IOMMU and KMD {}.", KMD_READ_ONLY_PAGE_PINNING.to_string()));
    }
    // Without an IOMMU the device sees physical addresses, so the buffer must be one contiguous range.
    const int flags = to_kmd_dma_flags(device_access) | (iommu_enabled ? TT_DMA_FLAG_NONE : TT_DMA_FLAG_CONTIGUOUS);
    uint64_t dma_address = 0;
    check_kmd_result(
        tt_pin_pages(tt_device_handle, buffer, size, flags, &dma_address, nullptr),
        fmt::format("Pinning {} bytes for DMA on {}", size, device_path));
    return dma_address;
}

bool PCIDevice::is_read_only_page_pinning_supported() const {
    return iommu_enabled && kmd_version >= KMD_READ_ONLY_PAGE_PINNING;
}

void PCIDevice::unmap_for_dma(void *buffer, size_t size) {
    check_kmd_result(
        tt_unpin_pages(tt_device_handle, buffer, size), fmt::format("Unpinning {} bytes on {}", size, device_path));
}

SemVer PCIDevice::read_kmd_version() {
    // {0, 0, 0} when the module is not loaded.
    const std::optional<std::string> version = read_sysfs_string("/sys/module/tenstorrent/version");
    if (!version.has_value() || version->empty()) {
        return SemVer{};
    }
    return SemVer(*version);
}

SemVer PCIDevice::read_kernel_version() {
    utsname system_info{};
    if (uname(&system_info) != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("uname() failed: {}", std::strerror(errno)));
    }
    unsigned int major = 0;
    unsigned int minor = 0;
    unsigned int patch = 0;
    std::sscanf(system_info.release, "%u.%u.%u", &major, &minor, &patch);
    return SemVer(major, minor, patch);
}

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(
    const size_t tlb_size, const TlbMapping tlb_mapping, const bool verify_config) {
    return std::make_unique<SiliconTlbHandle>(*this, tlb_size, tlb_mapping, verify_config);
}

void PCIDevice::configure_tlb(const uint32_t tlb_index, const tlb_data &tlb_config, const bool verify) {
    const ArchitectureTlbs &tlbs = get_architecture_tlbs(arch);
    const tlb_configuration configuration = tlbs.get_configuration(tlb_index);

    if (tlb_config.local_offset % configuration.size != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "TLB {} can only be pointed at a multiple of its size {:#x}, not at {:#x}.",
                tlb_index,
                configuration.size,
                tlb_config.local_offset));
    }
    // The register holds the address in units of the window size.
    tlb_data register_value = tlb_config;
    register_value.local_offset = tlb_config.local_offset / configuration.size;
    if (!register_value.check(configuration.offset)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("TLB {} configuration does not fit its register (a field is out of range).", tlb_index));
    }

    const auto [low, high] = register_value.apply_offset(configuration.offset);
    const uint32_t words[] = {
        static_cast<uint32_t>(low),
        static_cast<uint32_t>(low >> 32),
        static_cast<uint32_t>(high),
        static_cast<uint32_t>(high >> 32)};
    const size_t register_words = tlbs.cfg_reg_size_bytes / sizeof(uint32_t);

    auto *reg = reinterpret_cast<volatile uint32_t *>(
        static_cast<uint8_t *>(tlb_config_space) + (configuration.cfg_addr - tlbs.static_cfg_addr));
    for (size_t i = 0; i < register_words; i++) {
        reg[i] = words[i];
    }

    if (!verify) {
        return;
    }
    // The register writes are posted; reading them back both flushes them and confirms they landed.
    const auto deadline = std::chrono::steady_clock::now() + TLB_CONFIG_VERIFY_TIMEOUT;
    while (true) {
        bool matches = true;
        for (size_t i = 0; i < register_words; i++) {
            matches = matches && reg[i] == words[i];
        }
        if (matches) {
            return;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format("TLB {} configuration did not read back on {}.", tlb_index, device_path));
        }
        std::this_thread::yield();
    }
}

uint8_t PCIDevice::read_command_byte(const int pci_device_num) {
    // The PCI Command register sits at offset 4 of the config space.
    constexpr std::streamoff PCI_COMMAND_OFFSET = 4;
    const std::string config_path = sysfs_class_device_dir(pci_device_num) + "/config";
    std::ifstream config(config_path, std::ios::binary);
    char command = 0;
    if (!config || !config.seekg(PCI_COMMAND_OFFSET) || !config.read(&command, 1)) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to read the PCI command register from {}.", config_path));
    }
    return static_cast<uint8_t>(command);
}

void PCIDevice::send_reset_ioctl_to_devices(
    const std::unordered_set<int> &pci_target_devices, TenstorrentResetDevice flag, bool ignore_failures) {
    for (int pci_device_num : pci_target_devices) {
        const std::string path = fmt::format("{}/{}", DEVICE_NODE_DIR, pci_device_num);
        std::string failure;
        tt_device_t *device = nullptr;
        int result = tt_device_open(path.c_str(), &device, O_APPEND);
        if (result != 0) {
            failure = fmt::format("opening the device failed: {}", std::strerror(-result));
        } else {
            result = tt_device_reset(device, static_cast<uint32_t>(flag));
            if (result < 0) {
                failure = fmt::format("the reset ioctl failed: {}", std::strerror(-result));
            } else if (result != 0) {
                failure = "the driver reported the reset step failed";
            }
            tt_device_close(device);
        }

        if (failure.empty()) {
            log_debug(tt::LogUMD, "Sent reset {} to {}.", static_cast<uint32_t>(flag), path);
            continue;
        }
        const std::string message = fmt::format("Reset {} of {}: {}", static_cast<uint32_t>(flag), path, failure);
        if (!ignore_failures) {
            UMD_THROW(error::RuntimeError, message);
        }
        log_warning(tt::LogUMD, "{}", message);
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
    if (!is_tlb_dmabuf_export_supported()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Exporting a TLB window as a dma-buf needs KMD {} and Linux {}.",
                KMD_TLB_DMABUF_EXPORT.to_string(),
                MIN_KERNEL_TLB_DMABUF_EXPORT.to_string()));
    }
    const uint64_t page = page_size();
    if (size == 0 || offset % page != 0 || size % page != 0 || offset + size > window_size) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Cannot export {:#x} bytes at offset {:#x} of a {:#x}-byte TLB window: both must be page aligned, "
                "non-empty and inside the window.",
                size,
                offset,
                window_size));
    }

    tt_tlb_t *window = nullptr;
    check_kmd_result(
        tt_tlb_alloc(tt_device_handle, window_size, TT_MMIO_CACHE_MODE_UC, &window),
        fmt::format("Allocating a {:#x}-byte TLB window on {}", window_size, device_path));

    // The export holds its own reference on the window, so ours is released whatever happens.
    struct WindowRelease {
        tt_device_t *device;
        tt_tlb_t *window;

        ~WindowRelease() { tt_tlb_free(device, window); }
    } release{tt_device_handle, window};

    uint32_t window_id = 0;
    tt_tlb_get_id(window, &window_id);
    // Verified, since the window's next user is the importing device rather than this host.
    configure_tlb(window_id, config, /*verify=*/true);

    int dmabuf_fd = -1;
    check_kmd_result(
        tt_tlb_export_dmabuf(tt_device_handle, window, offset, size, &dmabuf_fd),
        fmt::format("Exporting TLB {} of {} as a dma-buf", window_id, device_path));
    return dmabuf_fd;
}

uint64_t PCIDevice::noc_read_scalar(uint64_t addr, uint32_t width, NocAddressSpace space) {
    uint64_t value = 0;
    check_kmd_result(
        tt_noc_read_scalar(
            tt_device_handle,
            addr,
            &value,
            width,
            space == NocAddressSpace::DeviceLocal ? TT_NOC_FLAG_KLA : TT_NOC_FLAG_NONE),
        fmt::format("Scalar NOC read of {} bytes at {:#x} on {}", width, addr, device_path));
    return value;
}

void PCIDevice::noc_write_scalar(uint64_t addr, uint64_t value, uint32_t width, NocAddressSpace space) {
    check_kmd_result(
        tt_noc_write_scalar(
            tt_device_handle,
            addr,
            value,
            width,
            space == NocAddressSpace::DeviceLocal ? TT_NOC_FLAG_KLA : TT_NOC_FLAG_NONE),
        fmt::format("Scalar NOC write of {} bytes at {:#x} on {}", width, addr, device_path));
}

void PCIDevice::set_power_state(bool busy) {
    if (kmd_version < KMD_POWER_STATE || arch != tt::ARCH::BLACKHOLE) {
        return;
    }
    const uint16_t power_flags = busy ? TT_POWER_FLAG_MAX_AI_CLK | TT_POWER_FLAG_MRISC_PHY_WAKEUP |
                                            TT_POWER_FLAG_TENSIX_ENABLE | TT_POWER_FLAG_L2CPU_ENABLE
                                      : 0;
    check_kmd_result(
        tt_device_set_power_state(tt_device_handle, power_flags),
        fmt::format("Setting the power state of {} to {}", device_path, busy ? "busy" : "idle"));
}

void PCIDevice::allocate_pcie_dma_buffer() {
    for (size_t size = MAX_PCIE_DMA_BUFFER_SIZE; size >= page_size(); size /= 2) {
        const bool allocated =
            iommu_enabled ? try_allocate_pcie_dma_buffer_iommu(size) : try_allocate_pcie_dma_buffer_no_iommu(size);
        if (allocated) {
            log_debug(tt::LogUMD, "Allocated a {}-byte PCIe DMA buffer for {}.", size, device_path);
            return;
        }
    }
    log_debug(tt::LogUMD, "No PCIe DMA buffer could be allocated for {}.", device_path);
}

// Both variants allocate one extra page past the buffer for the completion word the DMA engine writes.
bool PCIDevice::try_allocate_pcie_dma_buffer_iommu(const size_t dma_buf_size) {
    const size_t mapping_size = dma_buf_size + page_size();
    void *mapping = mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
            tt_device_handle,
            PCIE_DMA_BUFFER_INDEX,
            mapping_size,
            TT_DMA_FLAG_NONE,
            &mapping,
            &physical_address,
            nullptr) != 0) {
        return false;
    }
    dma_buffer.buffer = static_cast<uint8_t *>(mapping);
    dma_buffer.completion = dma_buffer.buffer + dma_buf_size;
    dma_buffer.size = dma_buf_size;
    dma_buffer.buffer_pa = physical_address;
    dma_buffer.completion_pa = physical_address + dma_buf_size;
    return true;
}

}  // namespace tt::umd
