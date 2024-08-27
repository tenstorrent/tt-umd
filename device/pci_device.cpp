/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <vector>
#include <architecture_implementation.hpp>

#include "pci_device.hpp"
#include "common/assert.hpp"
#include "common/logger.hpp"

int find_device(const uint16_t device_id) {
    // returns device id if found, otherwise -1
    char device_name[sizeof(device_name_pattern) + std::numeric_limits<unsigned int>::digits10];
    std::snprintf(device_name, sizeof(device_name), device_name_pattern, (unsigned int)device_id);
    int device_fd = ::open(device_name, O_RDWR | O_CLOEXEC);
    LOG2 ("find_device() open call returns device_fd: %d for device_name: %s (device_id: %d)\n", device_fd, device_name, device_id);
    return device_fd;
}

std::tuple<uint16_t, int> get_pcie_info(DWORD device_id) {
    // Get PCIe device info through IOTCL -> tt-kmd and return pci_device_id and revision_id
    std::uint16_t pcie_domain;
    std::uint8_t pcie_bus;
    std::uint8_t pcie_device;
    std::uint8_t pcie_function;

    int device_fd = find_device(device_id);
    if (device_fd == -1) {
        TT_THROW("Failed opening device handle for device: ", device_id);
    }
    tenstorrent_get_device_info device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.in.output_size_bytes = sizeof(device_info.out);
    if (ioctl(device_fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &device_info) == -1) {
        TT_THROW("Get PCIe device info failed on device: ", device_id);
    }
    pcie_domain = device_info.out.pcie_domain;
    pcie_bus = device_info.out.bus_dev_fn >> 8;
    pcie_device = PCI_SLOT(device_info.out.bus_dev_fn);
    pcie_function = PCI_FUNC(device_info.out.bus_dev_fn);

    // Get the PCIe revision ID from sysfs
    static const char pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/revision";
    char buf[sizeof(pattern)];
    std::snprintf(buf, sizeof(buf), pattern, pcie_domain, pcie_bus, pcie_device, pcie_function);

    std::ifstream revision_file(buf);
    std::string revision_string;
    if (std::getline(revision_file, revision_string)) {
        return std::make_tuple(device_info.out.device_id, std::stoi(revision_string, nullptr, 0));
    } else {
        TT_THROW("Revision ID read failed for device: ", device_id);
    }
}

tt::ARCH detect_arch(DWORD device_id) {
    auto pcie_info = get_pcie_info(device_id);
    uint16_t pcie_device_id = std::get<0>(pcie_info);
    int pcie_revision_id = std::get<1>(pcie_info);
    if (pcie_device_id == 0xfaca){
        return tt::ARCH::GRAYSKULL;
    } else if (pcie_device_id == 0x401e && pcie_revision_id == 0x01){
        return tt::ARCH::WORMHOLE_B0;
    } else if (pcie_device_id == 0x401e){
        return tt::ARCH::WORMHOLE;
    } else if (pcie_device_id == 0xb140){
        return tt:ARCH::BLACKHOLE;
    } else {
        TT_THROW("Unknown pcie device id that does not match any known architecture: ", pcie_device_id);
    }
}

// these should just be the constructor / destructor
PCIdevice ttkmd_open(DWORD device_id, bool sharable /* = false */)
{
    (void)sharable; // presently ignored

    auto ttdev = std::make_unique<TTDevice>(TTDevice::open(device_id));

    PCIdevice device;
    device.id = device_id;
    device.hdev = ttdev.get();
    device.vendor_id = ttdev->device_info.vendor_id;
    device.device_id = ttdev->device_info.device_id;
    device.subsystem_vendor_id = ttdev->device_info.subsystem_vendor_id;
    device.subsystem_id = ttdev->device_info.subsystem_id;
    device.dwBus = ttdev->pci_bus;
    device.dwSlot = ttdev->pci_device;
    device.dwFunction = ttdev->pci_function;
    device.BAR_addr = read_bar0_base(ttdev.get());
    device.BAR_size_bytes = ttdev->bar0_uc_size;
    device.revision_id = get_revision_id(ttdev.get());
    ttdev.release();

    return device;
}

// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------

TTDevice::TTDevice(DWORD device_id){
    this->device_id = device_id;
    open_device();
}

TTDevice::~TTDevice(){
    close_device();
}

void TTDevice::setup_device() {
    device_fd = find_device(device_id);
    if (device_fd == -1) {
        throw std::runtime_error(std::string("Failed opening a handle for device ") + std::to_string(device_id));
    }

    arch = detect_arch(this);
    architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch));

    // Get PCIe device info through IOTCL -> tt-kmd
    tenstorrent_get_device_info device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.in.output_size_bytes = sizeof(device_info.out);
    if (ioctl(device_fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &device_info) == -1) {
        throw std::runtime_error(std::string("Get device info failed on device ") + std::to_string(device_id) + ".");
    }

    // delete, just use the iotcl call when you need one of these params
    pcie_info.dwDomain = device_info.out.pcie_domain;
    pcie_info.dwBus = device_info.out.bus_dev_fn >> 8;
    pcie_info.dwSlot = PCI_SLOT(device_info.out.bus_dev_fn);
    pcie_info.dwFunction = PCI_FUNC(device_info.out.bus_dev_fn);

    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;

    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(device_fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(std::string("Query mappings failed on device ") + std::to_string(device_id) + ".");
    }

    // Mapping resource to BAR
    // Resource 0 -> BAR0
    // Resource 1 -> BAR2
    // Resource 2 -> BAR4
    tenstorrent_mapping bar0_uc_mapping;
    tenstorrent_mapping bar0_wc_mapping;
    tenstorrent_mapping bar2_uc_mapping;
    tenstorrent_mapping bar2_wc_mapping;
    tenstorrent_mapping bar4_uc_mapping;
    tenstorrent_mapping bar4_wc_mapping;

    memset(&bar0_uc_mapping, 0, sizeof(bar0_uc_mapping));
    memset(&bar0_wc_mapping, 0, sizeof(bar0_wc_mapping));
    memset(&bar2_uc_mapping, 0, sizeof(bar2_uc_mapping));
    memset(&bar2_wc_mapping, 0, sizeof(bar2_wc_mapping));
    memset(&bar4_uc_mapping, 0, sizeof(bar4_uc_mapping));
    memset(&bar4_wc_mapping, 0, sizeof(bar4_wc_mapping));

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

        log_debug(LogSiliconDriver, "BAR mapping id {} base {} size {}",
            mappings.mapping_array[i].mapping_id,
            (void *)mappings.mapping_array[i].mapping_base,
            mappings.mapping_array[i].mapping_size);
    }

    if (bar0_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE0_UC) {
        throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR0 UC mapping.");
    }

    auto wc_mapping_size = is_blackhole(device_info.out) ? BH_BAR0_WC_MAPPING_SIZE : GS_BAR0_WC_MAPPING_SIZE;

    // Attempt WC mapping first so we can fall back to all-UC if it fails.
    if (bar0_wc_mapping.mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
        bar0_wc_size = std::min<size_t>(bar0_wc_mapping.mapping_size, wc_mapping_size);
        bar0_wc = mmap(NULL, bar0_wc_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar0_wc_mapping.mapping_base);
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

    bar0_uc = mmap(NULL, bar0_uc_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar0_uc_mapping.mapping_base + bar0_uc_offset);

    if (bar0_uc == MAP_FAILED) {
        throw std::runtime_error(std::string("BAR0 UC memory mapping failed for device ") + std::to_string(device_id) + ".");
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (is_wormhole(device_info.out)) {
        if (bar4_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(device_id) + " has no BAR4 UC mapping.");
        }

        this->system_reg_mapping_size = bar4_uc_mapping.mapping_size;

        this->system_reg_mapping = mmap(NULL, bar4_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar4_uc_mapping.mapping_base);

        if (this->system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(std::string("BAR4 UC memory mapping failed for device ") + std::to_string(device_id) + ".");
        }

        this->system_reg_start_offset = (512 - 16) * 1024*1024;
        this->system_reg_offset_adjust = (512 - 32) * 1024*1024;
    } else if(is_blackhole(device_info.out)) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE1_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(device_id) + " has no BAR2 UC mapping.");
        }

        // Using UnCachable memory mode. This is used for accessing registers on Blackhole.
        this->bar2_uc_size = bar2_uc_mapping.mapping_size;
        this->bar2_uc = mmap(NULL, bar2_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar2_uc_mapping.mapping_base);

        if (this->bar2_uc == MAP_FAILED) {
            throw std::runtime_error(std::string("BAR2 UC memory mapping failed for device ") + std::to_string(device_id) + ".");
        }

        if (bar4_wc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_WC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(device_id) + " has no BAR4 WC mapping.");
        }

        // Using Write-Combine memory mode. This is used for accessing DRAM on Blackhole.
        // WC doesn't guarantee write ordering but has better performance.
        this->bar4_wc_size = bar4_wc_mapping.mapping_size;
        this->bar4_wc = mmap(NULL, bar4_wc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar4_wc_mapping.mapping_base);

        if (this->bar4_wc == MAP_FAILED) {
            throw std::runtime_error(std::string("BAR4 WC memory mapping failed for device ") + std::to_string(device_id) + ".");
        }
    }
    pcie_info.BAR_addr = 
    pcie_info.BAR_size_bytes = bar0_uc_size;

    // GS+WH: ARC_SCRATCH[6], BH: NOC NODE_ID
    this->read_checking_offset = is_blackhole(device_info.out) ? BH_NOC_NODE_ID_OFFSET : GS_WH_ARC_SCRATCH_6_OFFSET;
}

void TTDevice::close_device() {
    if (arch == tt::ARCH::BLACKHOLE && bar2_uc != nullptr && bar2_uc != MAP_FAILED) {
        // Disable ATU index 0
        // TODO: Implement disabling for all indexes, once more host channels are enabled.
        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;
        uint32_t region_ctrl_2 = 0 << 31; // REGION_EN = 0
        write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(bar2_uc) + iatu_base + 0x04), &region_ctrl_2, 1);
    }

    if (device_fd != -1) {
        close(device_fd);
    }

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

    if (sysfs_config_fd != -1) {
        close(sysfs_config_fd);
    }

    device_fd = -1;
    bar0_uc = nullptr;
    bar0_wc = nullptr;
    bar2_uc = nullptr;
    bar4_wc = nullptr;
    system_reg_mapping = nullptr;
    sysfs_config_fd = -1;
}

// Open a unique device_id per host memory channel (workaround for ttkmd < 1.21 support for more than 1 pin per fd)
void TTDevice::open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels) {
    for (int ch = 0; ch < num_host_mem_channels; ch++) {
        log_debug(LogSiliconDriver, "Opening device_fd_per_host_ch device index: {} ch: {} (num_host_mem_channels: {})", index, ch, num_host_mem_channels);
        int device_fd_for_host_mem = find_device(index);
        if (device_fd_for_host_mem == -1) {
            throw std::runtime_error(std::string("Failed opening a host memory device handle for device ") + std::to_string(index));
        }
        device_fd_per_host_ch.push_back(device_fd_for_host_mem);
    }
}

tt::ARCH TTDevice::get_arch() const {
    return arch;
}
