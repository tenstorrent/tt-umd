/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "common_types.h"
#include "ioctl.h"

#include <vector>

namespace tt::umd {

// Abstracts calls to kernel driver for PCI device management.
// Lowest layer of our driver.
// No chip type specific code here.
// Non abstract class.
class PCIDevice {
    public:
    // When you call the constructor you can get basic info on the device afterwards.
    PCIDevice(unsigned int device_id);

    int sysfs_config_fd = -1;
    std::uint16_t pci_domain;
    std::uint8_t pci_bus;
    std::uint8_t pci_device;
    std::uint8_t pci_function;
    uint32_t logical_id;
    Arch arch;

    Arch get_arch() const;
    int get_revision_id();
    int get_numa_node();


    // For all the functions following open() had to be called only after the device has been opened.
    // Open might lock some device resources in the future, but the constructor itself doesn't do that.
    void open(); // previously void resume_after_device_reset();
    void close(); // previously void suspend_before_device_reset();
    
    // Returns physical address, to be used by firmware which writes to sysmem.
    uint64_t pin_hugepage(uint64_t virtual_address, uint64_t hugepage_size); // previously void open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels);

    void *bar0_uc = nullptr;
    std::size_t bar0_uc_size = 0;
    std::size_t bar0_uc_offset = 0;

    void *bar0_wc = nullptr;
    std::size_t bar0_wc_size = 0;

    void *bar2_uc = nullptr;
    std::size_t bar2_uc_size;

    void *bar4_wc = nullptr;
    std::uint64_t bar4_wc_size;

    bool reset_by_sysfs();
    bool reset_by_ioctl();
};

}  // namespace tt::umd
