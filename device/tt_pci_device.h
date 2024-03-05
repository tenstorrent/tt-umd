// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <vector>
#include <cstdint>
#include <memory>

#include "ioctl.h"
#include "kmdif.h"
#include "tt_arch_types.h"
#include "architecture_implementation.h"

struct TTDevice
{
    const std::string device_path;
    const uint32_t index;
    const int device_fd;
    const tenstorrent_get_device_info_out device_info;
    const uint16_t pci_domain;
    const uint8_t pci_bus;
    const uint8_t pci_device;
    const uint8_t pci_function;
    const std::string pci_device_path;
    const int revision_id;
    const tt::ARCH arch;
    std::unique_ptr<tt::umd::architecture_implementation> architecture_implementation;

    void *bar0_uc = nullptr;
    std::size_t bar0_uc_size = 0;
    std::size_t bar0_uc_offset = 0;

    void *bar0_wc = nullptr;
    std::size_t bar0_wc_size = 0;

    void *system_reg_mapping = nullptr;
    std::size_t system_reg_mapping_size;

    void *system_reg_wc_mapping = nullptr;
    std::size_t system_reg_wc_mapping_size;

    std::uint32_t system_reg_start_offset;  // Registers >= this are system regs, use the mapping.
    std::uint32_t system_reg_offset_adjust; // This is the offset of the first reg in the system reg mapping.

    int sysfs_config_fd = -1;
    uint64_t bar0_base;

    unsigned int next_dma_buf = 0;

	DMAbuffer dma_completion_flag_buffer;  // When DMA completes, it writes to this buffer
	DMAbuffer dma_transfer_buffer;         // Buffer for large DMA transfers

    std::vector<DMAbuffer> dma_buffer_mappings;

public: // methods
    TTDevice(uint32_t device_index);

    tt::ARCH get_arch() const { return arch; }
    tt::umd::architecture_implementation* get_architecture_implementation() const { return architecture_implementation.get(); }

    int get_link_width() const;
    int get_link_speed() const;
    int get_revision_id() const;

    bool is_grayskull() const;
    bool is_wormhole() const;
    bool is_wormhole_b0() const;

    ~TTDevice() noexcept;
};
