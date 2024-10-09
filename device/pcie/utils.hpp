/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <fcntl.h>  // for ::open
#include <sys/ioctl.h> // for ioctl
#include <linux/pci.h> // for PCI_SLOT, PCI_FUNC

#include "ioctl.h"
#include "common/assert.hpp"
#include "device/tt_arch_types.h"

// PCIe device IDs through ioctl
static const uint16_t GS_PCIE_DEVICE_ID = 0xfaca;
static const uint16_t WH_PCIE_DEVICE_ID = 0x401e;
static const uint16_t BH_PCIE_DEVICE_ID = 0xb140;

int find_device(const uint16_t device_id) {
    const char device_name_pattern [] = "/dev/tenstorrent/%u";
    char device_name[sizeof(device_name_pattern) + std::numeric_limits<unsigned int>::digits10];
    snprintf(device_name, sizeof(device_name), device_name_pattern, (unsigned int)device_id);
    int device_fd = open(device_name, O_RDWR | O_CLOEXEC);
    if (device_fd == -1) {
        TT_THROW("Failed opening a handle for device ", device_id);
    }
    return device_fd;
}

tenstorrent_get_device_info get_pcie_device_info(int device_fd) {
    tenstorrent_get_device_info device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.in.output_size_bytes = sizeof(device_info.out);
    if (ioctl(device_fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &device_info) == -1) {
        TT_THROW("Get PCIe device info failed on device fd: ", device_fd);
    }
    return device_info;
}

std::uint32_t get_pcie_info(int device_id, const std::string &info_needed) {
    // Get PCIe device info through iotcl
    int device_fd = find_device(device_id);
    auto device_info = get_pcie_device_info(device_fd);

    if(info_needed == "pcie_device_id"){
        return device_info.out.device_id;
    }

    std::uint16_t pcie_domain = device_info.out.pci_domain;
    std::uint8_t pcie_bus = device_info.out.bus_dev_fn >> 8;
    std::uint8_t pcie_device = PCI_SLOT(device_info.out.bus_dev_fn);
    std::uint8_t pcie_function = PCI_FUNC(device_info.out.bus_dev_fn);

    // Get the PCIe info from sysfs
    static const char sys_pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/%s";
    char buf[sizeof(sys_pattern) + 10];
    snprintf(buf, sizeof(buf), sys_pattern, pcie_domain, pcie_bus, pcie_device, pcie_function, info_needed.c_str());
    std::ifstream pcie_info_file(buf);
    std::string pcie_info_string;

    if (!std::getline(pcie_info_file, pcie_info_string)) {
        TT_THROW("/sys/* read failed for device: ", device_id);
    }
    return std::stoul(pcie_info_string, nullptr, 0);
}

tt::ARCH detect_arch(std::uint32_t pcie_device_id, std::uint32_t pcie_revision_id) {
    if (pcie_device_id == GS_PCIE_DEVICE_ID){
        return tt::ARCH::GRAYSKULL;
    } else if (pcie_device_id == WH_PCIE_DEVICE_ID && pcie_revision_id == 0x01){
        return tt::ARCH::WORMHOLE_B0;
    } else if (pcie_device_id == WH_PCIE_DEVICE_ID){
        TT_THROW("Wormhole is not supported. Please use Wormhole B0 instead.");
        return tt::ARCH::WORMHOLE;
    } else if (pcie_device_id == WH_PCIE_DEVICE_ID){
        return tt::ARCH::BLACKHOLE;
    } else {
        TT_THROW("Unknown pcie device id that does not match any known architecture: ", pcie_device_id);
    }
}
