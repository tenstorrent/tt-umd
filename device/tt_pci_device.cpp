// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_pci_device.h"

#include <fcntl.h>
#include <fmt/format.h>
#include <linux/pci.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <sstream>

static uint32_t GS_BAR0_WC_MAPPING_SIZE = (156 << 20) + (10 << 21) + (18 << 24);

static uint32_t TTKMD_MIN_MAJOR_VERSION = 1;
static uint32_t TTKMD_MIN_MINOR_VERSION = 27;

static tenstorrent_get_device_info_out read_device_info(int fd) {
    tenstorrent_get_device_info device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.in.output_size_bytes = sizeof(device_info.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &device_info) == -1) {
        throw std::runtime_error("Get device info failed.");
    }

    return device_info.out;
}

static tt::ARCH determine_arch(TTDevice *device) {
    if (device->is_grayskull()) {
        return tt::ARCH::GRAYSKULL;
    } else if (device->is_wormhole_b0()) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (device->is_wormhole()) {
        return tt::ARCH::WORMHOLE;
    } else {
        throw std::runtime_error(std::string("Unknown architecture"));
    }

    return tt::ARCH::Invalid;
}

TTDevice::TTDevice(uint32_t device_index)
    : device_path(fmt::format("/dev/tenstorrent/{}", device_index))
    , index(device_index)
    , device_fd(::open(device_path.c_str(), O_RDWR | O_CLOEXEC))
    , device_info(read_device_info(device_fd))
    , pci_domain(device_info.pci_domain)
    , pci_bus(device_info.bus_dev_fn >> 8)
    , pci_device(PCI_SLOT(device_info.bus_dev_fn))
    , pci_function(PCI_FUNC(device_info.bus_dev_fn))
    , pci_device_path(fmt::format(
          "/sys/bus/pci/devices/{:04x}:{:02x}:{:02x}.{:01x}/", pci_domain, pci_bus, pci_device, pci_function))
    , revision_id(get_revision_id())
    , arch(determine_arch(this))
    , architecture_implementation(
          tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch))) {
    // Check version of TT KMD; if it is too old, throw an exception.
    std::ifstream version_file("/sys/module/tenstorrent/version");
    std::string version_string;
    if (std::getline(version_file, version_string)) {
        std::istringstream version_stream(version_string);
        uint32_t major, minor;
        char dot;
        version_stream >> major >> dot >> minor;
        if (major < TTKMD_MIN_MAJOR_VERSION || (major == TTKMD_MIN_MAJOR_VERSION && minor < TTKMD_MIN_MINOR_VERSION)) {
            throw std::runtime_error(fmt::format(
                "TTKMD version too old; expected >= {}.{}; actual: {}.{}",
                TTKMD_MIN_MAJOR_VERSION,
                TTKMD_MIN_MINOR_VERSION,
                major,
                minor));
        }
    } else {
        throw std::runtime_error("TTKMD version read failed.");
    }

// This doesn't work...
#if 0
    tenstorrent_get_driver_info driver_info;
    driver_info.in.output_size_bytes = sizeof(driver_info.out);
    if (ioctl(device_fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &driver_info) == -1) {
        throw std::runtime_error("Get driver info failed.");
    }
#endif

    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;

    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(device_fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(std::string("Query mappings failed on device ") + std::to_string(index) + ".");
    }

    tenstorrent_mapping bar0_uc_mapping;
    tenstorrent_mapping bar0_wc_mapping;
    tenstorrent_mapping bar2_uc_mapping;
    tenstorrent_mapping bar2_wc_mapping;

    memset(&bar0_uc_mapping, 0, sizeof(bar0_uc_mapping));
    memset(&bar0_wc_mapping, 0, sizeof(bar0_wc_mapping));
    memset(&bar2_uc_mapping, 0, sizeof(bar2_uc_mapping));
    memset(&bar2_wc_mapping, 0, sizeof(bar2_wc_mapping));

    for (unsigned int i = 0; i < mappings.query_mappings.in.output_mapping_count; i++) {
        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_UC) {
            bar0_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
            bar0_wc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_UC) {
            bar2_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_WC) {
            bar2_wc_mapping = mappings.mapping_array[i];
        }
    }

    if (bar0_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE0_UC) {
        throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR0 UC mapping.");
    }

    // Attempt WC mapping first so we can fall back to all-UC if it fails.
    if (bar0_wc_mapping.mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
        bar0_wc_size = std::min<size_t>(bar0_wc_mapping.mapping_size, GS_BAR0_WC_MAPPING_SIZE);
        bar0_wc = mmap(NULL, bar0_wc_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar0_wc_mapping.mapping_base);
        if (bar0_wc == MAP_FAILED) {
            bar0_wc_size = 0;
            bar0_wc = nullptr;
        }
    }

    if (bar0_wc) {
        // The bottom part of the BAR is mapped WC. Map the top UC.
        bar0_uc_size = bar0_uc_mapping.mapping_size - GS_BAR0_WC_MAPPING_SIZE;
        bar0_uc_offset = GS_BAR0_WC_MAPPING_SIZE;
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
        device_fd,
        bar0_uc_mapping.mapping_base + bar0_uc_offset);

    if (bar0_uc == MAP_FAILED) {
        throw std::runtime_error(
            std::string("BAR0 UC memory mapping failed for device ") + std::to_string(index) + ".");
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (this->is_wormhole()) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR4 UC mapping.");
        }

        this->system_reg_mapping_size = bar2_uc_mapping.mapping_size;

        this->system_reg_mapping = mmap(
            NULL,
            bar2_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            device_fd,
            bar2_uc_mapping.mapping_base);

        if (this->system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(
                std::string("BAR4 UC memory mapping failed for device ") + std::to_string(index) + ".");
        }

        this->system_reg_start_offset = (512 - 16) * 1024 * 1024;
        this->system_reg_offset_adjust = (512 - 32) * 1024 * 1024;
    }

    const std::string config_path = pci_device_path + "config";
    sysfs_config_fd = open(config_path.c_str(), O_RDONLY);
    if (sysfs_config_fd != -1) {
        const uint64_t bar_address_mask = ~(std::uint64_t)0xF;
        unsigned int bar0_config_offset = 0x10;

        uint64_t bar01;
        if (pread(sysfs_config_fd, &bar01, sizeof(bar01), bar0_config_offset) == sizeof(bar01)) {
            bar0_base = bar01 & bar_address_mask;
        }
    }
}

int TTDevice::get_link_width() const {
    std::ifstream linkwidth_file(pci_device_path + "current_link_width");
    std::string linkwidth_string;
    if (std::getline(linkwidth_file, linkwidth_string)) {
        return std::stoi(linkwidth_string, nullptr, 0);
    } else {
        throw std::runtime_error("Link width read failed for device");
    }
}

int TTDevice::get_link_speed() const {
    std::ifstream linkspeed_file(pci_device_path + "current_link_speed");
    std::string linkspeed_string;
    int linkspeed;
    if (std::getline(linkspeed_file, linkspeed_string) && sscanf(linkspeed_string.c_str(), "%d", &linkspeed) == 1) {
        return linkspeed;
    } else {
        throw std::runtime_error("Link speed read failed for device");
    }
}

int TTDevice::get_revision_id() const {
    std::ifstream revision_file(pci_device_path + "revision");
    std::string revision_string;
    if (std::getline(revision_file, revision_string)) {
        return std::stoi(revision_string, nullptr, 0);
    } else {
        throw std::runtime_error("Revision ID read failed for device");
    }
}

bool TTDevice::is_grayskull() const {
    return device_info.device_id == 0xfaca;
}

bool TTDevice::is_wormhole() const {
    return device_info.device_id == 0x401e;
}

bool TTDevice::is_wormhole_b0() const {
    return is_wormhole() && this->revision_id == 1;
}

TTDevice::~TTDevice() noexcept {
    if (device_fd != -1) {
        close(device_fd);
    }

    if (bar0_wc != nullptr && bar0_wc != MAP_FAILED && bar0_wc != bar0_uc) {
        munmap(bar0_wc, bar0_wc_size);
    }

    if (bar0_uc != nullptr && bar0_uc != MAP_FAILED) {
        munmap(bar0_uc, bar0_uc_size);
    }

    if (system_reg_mapping != nullptr && system_reg_mapping != MAP_FAILED) {
        munmap(system_reg_mapping, system_reg_mapping_size);
    }

    for (auto &&buf : dma_buffer_mappings) {
        munmap(buf.pBuf, buf.size);
    }

    if (sysfs_config_fd != -1) {
        close(sysfs_config_fd);
    }
}