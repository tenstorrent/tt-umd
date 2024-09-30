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

// Stash all the fields of PCIDevice in PCIDeviceBase to make moving simpler.
class PCIDeviceBase {
    public:
    unsigned int index;

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

    void *system_reg_wc_mapping = nullptr;
    std::size_t system_reg_wc_mapping_size;

    std::uint32_t system_reg_start_offset;   // Registers >= this are system regs, use the mapping.
    std::uint32_t system_reg_offset_adjust;  // This is the offset of the first reg in the system reg mapping.

    int sysfs_config_fd = -1;
    std::uint16_t pci_domain;
    std::uint8_t pci_bus;
    std::uint8_t pci_device;
    std::uint8_t pci_function;

    tenstorrent_get_device_info_out device_info;

    std::uint32_t read_checking_offset;

    Arch arch;
    uint32_t logical_id;
};

class PCIDevice : public PCIDeviceBase {
    public:
    PCIDevice(unsigned int device_id);
    void open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels);
    ~PCIDevice();

    PCIDevice(const PCIDevice &) = delete;
    void operator=(const PCIDevice &) = delete;

    PCIDevice(PCIDevice &&that);
    PCIDevice &operator=(PCIDevice &&that);

    void suspend_before_device_reset();

    void resume_after_device_reset();

    Arch get_arch() const;

    int get_config_space_fd();

    int get_revision_id();

    Arch detect_arch();

    static Arch detect_arch_from_device_id(unsigned int device_id = 0);

    int get_numa_node();

    std::uint64_t read_bar0_base();

    template <class T>
    volatile T *register_address(std::uint32_t register_offset) {
        void *reg_mapping;
        if (system_reg_mapping != nullptr && register_offset >= system_reg_start_offset) {
            register_offset -= system_reg_offset_adjust;
            reg_mapping = system_reg_mapping;
        } else if (bar0_wc != bar0_uc && register_offset < bar0_wc_size) {
            reg_mapping = bar0_wc;
        } else {
            register_offset -= bar0_uc_offset;
            reg_mapping = bar0_uc;
        }

        return reinterpret_cast<T *>(static_cast<uint8_t *>(reg_mapping) + register_offset);
    }

    bool reset_by_sysfs();

    bool reset_by_ioctl();

    // brosko: move stuff which is not logically here to TTDevice
    // brosko: this register_address is confusing, I think that something is hiding which chip is it.
    // void read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t *buffer_addr);
    // void write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t *buffer_addr);

    void write_regs(uint32_t byte_addr, uint32_t word_len, const void *data);

    void write_tlb_reg(
        uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size);

    void read_regs(uint32_t byte_addr, uint32_t word_len, void *data);

   private:
    void reset();

    void drop();

    void do_open();

};

}  // namespace tt::umd
