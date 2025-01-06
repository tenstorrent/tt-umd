/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/pci_device.hpp"

#include <fcntl.h>      // for ::open
#include <linux/pci.h>  // for PCI_SLOT, PCI_FUNC
#include <numa.h>
#include <sys/ioctl.h>  // for ioctl
#include <sys/mman.h>   // for mmap, munmap
#include <sys/stat.h>   // for fstat
#include <unistd.h>     // for ::close

#include <cstdint>
#include <cstring>  // for memcpy
#include <vector>

#include "assert.hpp"
#include "ioctl.h"
#include "logger.hpp"
#include "umd/device/hugepage.h"
#include "umd/device/types/arch.h"

static const uint16_t GS_PCIE_DEVICE_ID = 0xfaca;
static const uint16_t WH_PCIE_DEVICE_ID = 0x401e;
static const uint16_t BH_PCIE_DEVICE_ID = 0xb140;

// TODO: we'll have to rethink this when KMD takes control of the inbound PCIe
// TLB windows and there is no longer a pre-defined WC/UC split.
static const uint32_t GS_BAR0_WC_MAPPING_SIZE = (156 << 20) + (10 << 21) + (18 << 24);

// Defines the address for WC region. addresses 0 to BH_BAR0_WC_MAPPING_SIZE are in WC, above that are UC
static const uint32_t BH_BAR0_WC_MAPPING_SIZE = 188 << 21;

// Hugepages must be 1GB in size
const uint32_t HUGEPAGE_REGION_SIZE = 1 << 30;  // 1GB

static const char *HUGEPAGE_FAIL_MSG =
    "Failed to allocate or map an adequate quantity of 1GB hugepages.  This is a critical error and will prevent the "
    "application from functioning correctly.  Please ensure the system has enough 1GB hugepages available and that the "
    "application is requesting an appropriate number of hugepages per device.  This per-device quantity is also "
    "referred to as the number of host memory channels.  Helpful files to examine for debugging are as follows\n"
    "  /proc/cmdline\n"
    "  /proc/meminfo\n"
    "  /proc/buddyinfo\n"
    "  /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages\n"
    "  /sys/kernel/mm/hugepages/hugepages-1048576kB/free_hugepages\n"
    "  /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages\n"
    "  /sys/devices/system/node/node1/hugepages/hugepages-1048576kB/nr_hugepages\n"
    "  /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/free_hugepages\n"
    "  /sys/devices/system/node/node1/hugepages/hugepages-1048576kB/free_hugepages\n"
    "The application will now terminate.";

using namespace tt;
using namespace tt::umd;

template <typename T>
static T read_sysfs(const PciDeviceInfo &device_info, const std::string &attribute_name) {
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

    if (!std::getline(attribute_file, value_str)) {
        TT_THROW("Failed reading sysfs attribute: {}", sysfs_path);
    }

    std::istringstream iss(value_str);

    // Handle hexadecimal input for integer types
    if constexpr (std::is_integral_v<T>) {
        if (value_str.substr(0, 2) == "0x") {
            iss >> std::hex;
        }
    }

    if (!(iss >> value)) {
        TT_THROW("Failed to parse sysfs attribute value: {}", value_str);
    }

    return value;
}

template <typename T>
T read_sysfs(const PciDeviceInfo &device_info, const std::string &attribute_name, const T &default_value) {
    try {
        return read_sysfs<T>(device_info, attribute_name);
    } catch (...) {
        return default_value;
    }
}

static bool detect_iommu(const PciDeviceInfo &device_info) {
    try {
        auto iommu_type = read_sysfs<std::string>(device_info, "iommu_group/type");
        return iommu_type.substr(0, 3) == "DMA";  // DMA or DMA-FQ
    } catch (...) {
        return false;
    }
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

    return PciDeviceInfo{info.out.vendor_id, info.out.device_id, info.out.pci_domain, bus, dev, fn};
}

tt::ARCH PciDeviceInfo::get_arch() const {
    if (this->device_id == GS_PCIE_DEVICE_ID) {
        return tt::ARCH::GRAYSKULL;
    } else if (this->device_id == WH_PCIE_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (this->device_id == BH_PCIE_DEVICE_ID) {
        return tt::ARCH::BLACKHOLE;
    }
    return tt::ARCH::Invalid;
}

/* static */ std::vector<int> PCIDevice::enumerate_devices() {
    std::vector<int> device_ids;
    std::string path = "/dev/tenstorrent/";

    if (!std::filesystem::exists(path)) {
        return device_ids;
    }
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        std::string filename = entry.path().filename().string();

        // TODO: this will skip any device that has a non-numeric name, which
        // is probably what we want longer-term (i.e. a UUID or something).
        if (std::all_of(filename.begin(), filename.end(), ::isdigit)) {
            device_ids.push_back(std::stoi(filename));
        }
    }

    std::sort(device_ids.begin(), device_ids.end());
    return device_ids;
}

/* static */ std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    std::map<int, PciDeviceInfo> infos;
    for (int n : PCIDevice::enumerate_devices()) {
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

static const semver_t kmd_ver_for_iommu = semver_t(1, 29, 0);

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(fmt::format("/dev/tenstorrent/{}", pci_device_number)),
    pci_device_num(pci_device_number),
    pci_device_file_desc(open(device_path.c_str(), O_RDWR | O_CLOEXEC)),
    info(read_device_info(pci_device_file_desc)),
    numa_node(read_sysfs<int>(info, "numa_node", -1)),  // default to -1 if not found
    revision(read_sysfs<int>(info, "revision")),
    arch(info.get_arch()),
    kmd_version(read_kmd_version()),
    iommu_enabled(detect_iommu(info)) {
    if (iommu_enabled && kmd_version < kmd_ver_for_iommu) {
        TT_THROW("Running with IOMMU support requires KMD version {} or newer", kmd_ver_for_iommu.to_string());
    }

    log_info(
        LogSiliconDriver,
        "Opened PCI device {}; KMD version: {}, IOMMU: {}",
        pci_device_num,
        kmd_version.to_string(),
        iommu_enabled ? "enabled" : "disabled");

    log_assert(arch != tt::ARCH::WORMHOLE_B0 || revision == 0x01, "Wormhole B0 must have revision 0x01");

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
            LogSiliconDriver,
            "BAR mapping id {} base {} size {}",
            mappings.mapping_array[i].mapping_id,
            (void *)mappings.mapping_array[i].mapping_base,
            mappings.mapping_array[i].mapping_size);
    }

    if (bar0_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE0_UC) {
        throw std::runtime_error(fmt::format("Device {} has no BAR0 UC mapping.", pci_device_num));
    }

    // TODO: Move arch specific code to tt_device.
    // wc_mapping_size along with some ifelses below.
    auto wc_mapping_size = arch == tt::ARCH::BLACKHOLE ? BH_BAR0_WC_MAPPING_SIZE : GS_BAR0_WC_MAPPING_SIZE;

    // Attempt WC mapping first so we can fall back to all-UC if it fails.
    if (bar0_wc_mapping.mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
        bar0_wc_size = std::min<size_t>(bar0_wc_mapping.mapping_size, wc_mapping_size);
        bar0_wc = mmap(
            NULL, bar0_wc_size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, bar0_wc_mapping.mapping_base);
        if (bar0_wc == MAP_FAILED) {
            bar0_wc_size = 0;
            bar0_wc = nullptr;
        }
    }

    if (bar0_wc) {
        // The bottom part of the BAR is mapped WC. Map the top UC.
        bar0_uc_size = bar0_uc_mapping.mapping_size - wc_mapping_size;
        bar0_uc_offset = wc_mapping_size;
    } else {
        // No WC mapping, map the entire BAR UC.
        bar0_uc_size = bar0_uc_mapping.mapping_size;
        bar0_uc_offset = 0;
    }

    bar0_uc = mmap(
        NULL,
        bar0_uc_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        pci_device_file_desc,
        bar0_uc_mapping.mapping_base + bar0_uc_offset);

    if (bar0_uc == MAP_FAILED) {
        throw std::runtime_error(fmt::format("BAR0 UC mapping failed for device {}.", pci_device_num));
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (arch == tt::ARCH::WORMHOLE_B0) {
        if (bar4_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 UC mapping.", pci_device_num));
        }

        system_reg_mapping_size = bar4_uc_mapping.mapping_size;

        system_reg_mapping = mmap(
            NULL,
            bar4_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            pci_device_file_desc,
            bar4_uc_mapping.mapping_base);

        if (system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR4 UC mapping failed for device {}.", pci_device_num));
        }

        system_reg_start_offset = (512 - 16) * 1024 * 1024;
        system_reg_offset_adjust = (512 - 32) * 1024 * 1024;
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
}

PCIDevice::~PCIDevice() {
    for (const auto &hugepage_mapping : hugepage_mapping_per_channel) {
        if (hugepage_mapping.mapping) {
            munmap(hugepage_mapping.mapping, hugepage_mapping.mapping_size);
        }
    }

    close(pci_device_file_desc);

    if (bar0_wc != nullptr && bar0_wc != MAP_FAILED && bar0_wc != bar0_uc) {
        munmap(bar0_wc, bar0_wc_size);
    }

    if (bar0_uc != nullptr && bar0_uc != MAP_FAILED) {
        munmap(bar0_uc, bar0_uc_size);
    }

    if (bar2_uc != nullptr && bar2_uc != MAP_FAILED) {
        munmap(bar2_uc, bar2_uc_size);
    }

    if (bar4_wc != nullptr && bar4_wc != MAP_FAILED) {
        munmap(bar4_wc, bar4_wc_size);
    }

    if (system_reg_mapping != nullptr && system_reg_mapping != MAP_FAILED) {
        munmap(system_reg_mapping, system_reg_mapping_size);
    }
}

void PCIDevice::init_hugepage(uint32_t num_host_mem_channels) {
    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;

    if (numa_node > numa_max_node()) {
        log_warning(LogSiliconDriver, "numa_node: {} is greater than numa_max_node: {}.", numa_node, numa_max_node());
    }

    // Prefer allocations on the NUMA node associated with the device.
    numa_set_preferred(numa_node);

    if (is_iommu_enabled()) {
        size_t size = hugepage_size * num_host_mem_channels;
        init_iommu(size);
        return;
    }

    auto physical_device_id = get_device_num();
    std::string hugepage_dir = find_hugepage_dir(hugepage_size);
    if (hugepage_dir.empty()) {
        log_fatal("init_hugepage: no huge page mount found for hugepage_size: {}.", hugepage_size);
    }

    bool success = true;

    hugepage_mapping_per_channel.resize(num_host_mem_channels);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < num_host_mem_channels; ch++) {
        int hugepage_fd = open_hugepage_file(hugepage_dir, physical_device_id, ch);
        if (hugepage_fd == -1) {
            // Probably a permissions problem.
            log_warning(
                LogSiliconDriver,
                "init_hugepage: physical_device_id: {} ch: {} creating hugepage mapping file failed.",
                physical_device_id,
                ch);
            success = false;
            break;
        }

        // Verify opened file size.
        struct stat hugepage_st;
        if (fstat(hugepage_fd, &hugepage_st) == -1) {
            log_warning(LogSiliconDriver, "Error reading hugepage file size after opening.");
        }

        std::byte *mapping = static_cast<std::byte *>(
            mmap(nullptr, hugepage_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, hugepage_fd, 0));

        close(hugepage_fd);

        if (mapping == MAP_FAILED) {
            log_warning(
                LogSiliconDriver,
                "Mapping a hugepage failed. (device: {}, channel {}/{}, errno: {}).",
                physical_device_id,
                ch + 1,
                num_host_mem_channels,
                strerror(errno));
            if (hugepage_st.st_size == 0) {
                log_warning(
                    LogSiliconDriver,
                    "Opened hugepage file has zero size, mapping might've failed due to that. Verify that enough "
                    "hugepages are provided.");
            }
            print_file_contents("/proc/cmdline");
            print_file_contents(
                "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages");  // Hardcoded for 1GB hugepage.
            success = false;
            break;
        }

        tenstorrent_pin_pages pin_pages;
        memset(&pin_pages, 0, sizeof(pin_pages));
        pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
        pin_pages.in.flags = TENSTORRENT_PIN_PAGES_CONTIGUOUS;
        pin_pages.in.virtual_address = reinterpret_cast<std::uintptr_t>(mapping);
        pin_pages.in.size = hugepage_size;

        auto fd = get_fd();

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) == -1) {
            log_warning(LogSiliconDriver, "Failed to pin pages (errno: {}).", strerror(errno));
            munmap(mapping, hugepage_size);
            print_file_contents("/sys/module/tenstorrent/version", "(TTKMD version)");
            print_file_contents("/proc/meminfo");
            print_file_contents("/proc/buddyinfo");
            success = false;
            break;
        }

        hugepage_mapping_per_channel[ch] = {mapping, hugepage_size, pin_pages.out.physical_address};

        log_info(
            LogSiliconDriver,
            "init_hugepage: {} ch: {} mapping_size: {} physical address 0x{:x}",
            device_path,
            ch,
            hugepage_size,
            pin_pages.out.physical_address);
    }

    if (!success) {
        log_error(HUGEPAGE_FAIL_MSG);
        std::terminate();
    }
}

void PCIDevice::init_iommu(size_t size) {
    const size_t num_fake_mem_channels = size / HUGEPAGE_REGION_SIZE;

    if (!is_iommu_enabled()) {
        TT_THROW("IOMMU is required for sysmem without hugepages.");
    }

    log_info(LogSiliconDriver, "Allocating sysmem without hugepages (size: {:#x}).", size);
    void *mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    if (mapping == MAP_FAILED) {
        TT_THROW(
            "UMD: Failed to allocate memory for device/host shared buffer (size: {} errno: {}).",
            size,
            strerror(errno));
    }

    uint64_t iova = map_for_dma(mapping, size);
    log_info(LogSiliconDriver, "Mapped sysmem without hugepages to IOVA {:#x}.", iova);

    hugepage_mapping_per_channel.resize(num_fake_mem_channels);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (size_t ch = 0; ch < num_fake_mem_channels; ch++) {
        uint8_t *base = static_cast<uint8_t *>(mapping) + ch * HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel[ch] = {base, HUGEPAGE_REGION_SIZE, iova + ch * HUGEPAGE_REGION_SIZE};
    }
}

size_t PCIDevice::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

hugepage_mapping PCIDevice::get_hugepage_mapping(size_t channel) const {
    if (hugepage_mapping_per_channel.size() <= channel) {
        return {nullptr, 0, 0};
    } else {
        return hugepage_mapping_per_channel[channel];
    }
}

uint64_t PCIDevice::map_for_dma(void *buffer, size_t size) {
    static const auto page_size = sysconf(_SC_PAGESIZE);

    const uint64_t vaddr = reinterpret_cast<uint64_t>(buffer);
    const uint32_t flags = is_iommu_enabled() ? 0 : TENSTORRENT_PIN_PAGES_CONTIGUOUS;

    if (vaddr % page_size != 0 || size % page_size != 0) {
        TT_THROW("Buffer must be page-aligned with a size that is a multiple of the page size");
    }

    tenstorrent_pin_pages pin_pages{};
    pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
    pin_pages.in.flags = flags;
    pin_pages.in.virtual_address = vaddr;
    pin_pages.in.size = size;

    // With IOMMU, this will probably fail on you if you're mapping something
    // large.  The situation today is that the kernel driver uses a 32-bit DMA
    // address mask, so all DMA allocations and mappings show up in the IOVA
    // range of 0x0 to 0xffff'ffff.  According to syseng, we can get up to 3GB
    // on Intel, 3.75GB on AMD, but this requires multiple mappings with small
    // chunks, down to 2MB.  It's possible to make such non-contiguous mappings
    // appear both virtually contiguous (to the application) and physically
    // contiguous (to the NOC, using iATU), but it's not clear that this is
    // worth the effort...  the scheme this is intended to replace supports up
    // to 4GB which is what application developers want.
    //
    // What can we do here?
    // 1. Use hugepages (part of what we are trying to avoid here).
    // 2. Use a larger value for the driver's dma_address_bits (currently 32;
    //    has implications for non-UMD based applications -- basically that any
    //    DMA buffer mapped beyond the 4GB boundary requires iATU configuration
    //    for the hardware to be able to reach it).
    // 3. Use multiple mappings with small chunks (won't get us to 4GB; adds
    //    complexity).
    // 4. Modify the driver so that DMA allocations are in the low 4GB IOVA
    //    range but mappings from userspace can be further up (requires driver
    //    changes).
    // 5. ???
    //
    // If you need a quick workaround here, I suggest:
    //   sudo insmod ./tenstorrent.ko dma_address_bits=48
    if (ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) == -1) {
        TT_THROW("Failed to pin pages for DMA: {}", strerror(errno));
    }

    return pin_pages.out.physical_address;
}

void PCIDevice::print_file_contents(std::string filename, std::string hint) {
    if (std::filesystem::exists(filename)) {
        std::ifstream meminfo(filename);
        if (meminfo.is_open()) {
            std::cout << std::endl << "File " << filename << " " << hint << " is: " << std::endl;
            std::cout << meminfo.rdbuf();
        }
    }
}

semver_t PCIDevice::read_kmd_version() {
    static const std::string path = "/sys/module/tenstorrent/version";
    std::ifstream file(path);

    if (!file.is_open()) {
        log_warning(LogSiliconDriver, "Failed to open file: {}", path);
        return {0, 0, 0};
    }

    std::string version_str;
    std::getline(file, version_str);

    return semver_t(version_str);
}
