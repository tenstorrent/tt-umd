/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cstdint>
#include <vector>
#include <architecture_implementation.hpp>

typedef std::uint32_t DWORD;

struct TTDeviceAddressInfo {
    int device_fd = -1;
    std::vector<int> device_fd_per_host_ch;
    void *bar0_uc = nullptr;
    std::size_t bar0_uc_size = 0;
    std::size_t bar0_uc_offset = 0;

    void *bar0_wc = nullptr;
    std::size_t bar0_wc_size = 0;

    void *bar2_uc = nullptr;
    std::size_t bar2_uc_size;

    void *bar4_wc = nullptr;
    std::uint64_t bar4_wc_size;

    void *system_reg_mapping = nullptr;
    std::size_t system_reg_mapping_size;

    // These two are currently not used.
    void *system_reg_wc_mapping = nullptr;
    std::size_t system_reg_wc_mapping_size;

    std::uint32_t system_reg_start_offset;  // Registers >= this are system regs, use the mapping.
    std::uint32_t system_reg_offset_adjust; // This is the offset of the first reg in the system reg mapping.

    int sysfs_config_fd = -1;

    std::uint32_t read_checking_offset;
};

struct TTPCIeDeviceInfo {
    std::uint16_t vendor_id;
    std::uint16_t device_id;
    std::uint16_t subsystem_vendor_id;
    std::uint16_t subsystem_id;
    std::uint16_t revision_id;

    // PCI bus identifiers
    DWORD dwDomain;
	DWORD dwBus;
    DWORD dwSlot;
    DWORD dwFunction;

	uint64_t BAR_addr;
	DWORD BAR_size_bytes;
};

class TTDevice {
    public:
    void open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels);

    bool reset_board()

    unsigned int device_id;
    unsigned int logical_id;
    int device_fd = -1;

    // BAR and regs mapping setup
    std::vector<int> device_fd_per_host_ch;
    void *bar0_uc = nullptr;
    std::size_t bar0_uc_size = 0;
    std::size_t bar0_uc_offset = 0;

    void *bar0_wc = nullptr;
    std::size_t bar0_wc_size = 0;

    void *bar2_uc = nullptr;
    std::size_t bar2_uc_size;

    void *bar4_wc = nullptr;
    std::uint64_t bar4_wc_size;

    void *system_reg_mapping = nullptr;
    std::size_t system_reg_mapping_size;

    // These two are currently not used.
    void *system_reg_wc_mapping = nullptr;
    std::size_t system_reg_wc_mapping_size;

    std::uint32_t system_reg_start_offset;  // Registers >= this are system regs, use the mapping.
    std::uint32_t system_reg_offset_adjust; // This is the offset of the first reg in the system reg mapping.

    int sysfs_config_fd = -1;
    std::uint32_t read_checking_offset;

    tt::ARCH get_arch() const;
    
    private:
    TTDevice(DWORD device_id);
    ~TTDevice();
    TTDevice(const TTDevice&) = delete; // copy
    void operator = (const TTDevice&) = delete; // copy assignment

    void setup_device()
    void close_device();
    void drop();

    bool reset_by_sysfs();
    bool reset_by_ioctl()

    tt::ARCH arch;
    std::unique_ptr<tt::umd::architecture_implementation> architecture_implementation;
};
