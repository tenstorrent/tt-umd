// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "blackhole_tt_device.h"


// BAR0 size for Blackhole, used to determine whether write block should use BAR0 or BAR4
const uint64_t BAR0_BH_SIZE = 512 * 1024 * 1024;

// TLB size for DRAM on blackhole - 4GB
const uint64_t BH_4GB_TLB_SIZE = 4ULL * 1024 * 1024 * 1024;

// See /vendor_ip/synopsys/052021/bh_pcie_ctl_gen5/export/configuration/DWC_pcie_ctl.h
const uint64_t UNROLL_ATU_OFFSET_BAR = 0x1200;

namespace tt::umd {


// brosko: abolish this
inline void write_regs_nonclass(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

std::tuple<xy_pair, xy_pair> BlackholeTTDevice::multicast_workaround(xy_pair start, xy_pair end) const {
    // TODO: This is copied from wormhole_implementation. It should be implemented properly.

    // When multicasting there is a rare case where including the multicasting node in the box can result in a backup
    // and the multicasted data not reaching all endpoints specified. As a workaround we exclude the pci endpoint from
    // the multicast. This doesn't cause any problems with making some tensix cores inaccessible because column 0 (which
    // we are excluding) doesn't have tensix.
    start.x = start.x == 0 ? 1 : start.x;
    return std::make_tuple(start, end);
}

tlb_configuration BlackholeTTDevice::get_tlb_configuration(uint32_t tlb_index) const {

    // If TLB index is in range for 4GB tlbs (8 TLBs after 202 TLBs for 2MB)
    if (tlb_index >= blackhole::TLB_COUNT_2M && tlb_index < blackhole::TLB_COUNT_2M + blackhole::TLB_COUNT_4G) {
        return tlb_configuration {
            .size = blackhole::DYNAMIC_TLB_4G_SIZE,
            .base = blackhole::DYNAMIC_TLB_4G_BASE,
            .cfg_addr = blackhole::DYNAMIC_TLB_4G_CFG_ADDR,
            .index_offset = tlb_index - blackhole::TLB_BASE_INDEX_4G,
            .offset = blackhole::TLB_4G_OFFSET,
        };
    }
    
    return tlb_configuration{
        .size = blackhole::DYNAMIC_TLB_2M_SIZE,
        .base = blackhole::DYNAMIC_TLB_2M_BASE,
        .cfg_addr = blackhole::DYNAMIC_TLB_2M_CFG_ADDR,
        .index_offset = tlb_index - blackhole::TLB_BASE_INDEX_2M,
        .offset = blackhole::TLB_2M_OFFSET,
    };
}

std::optional<std::tuple<std::uint64_t, std::uint64_t>> BlackholeTTDevice::describe_tlb(
    std::int32_t tlb_index) const {
    std::uint32_t TLB_COUNT_2M = 202;

    std::uint32_t TLB_BASE_2M = 0;
    if (tlb_index < 0) {
        return std::nullopt;
    }

    if (tlb_index >= TLB_COUNT_2M && tlb_index < TLB_COUNT_2M + blackhole::TLB_COUNT_4G) {
        auto tlb_offset = tlb_index - TLB_COUNT_2M;
        auto size = blackhole::TLB_4G_SIZE;
        return std::tuple(blackhole::TLB_BASE_4G + tlb_offset * size, size);
    }

    if (tlb_index >= 0 && tlb_index < TLB_COUNT_2M) {
        auto tlb_offset = tlb_index;
        auto size = 1 << 21;
        return std::tuple(TLB_BASE_2M + tlb_offset * size, size);
    }

    return std::nullopt;
}

std::pair<std::uint64_t, std::uint64_t> BlackholeTTDevice::get_tlb_data(
    std::uint32_t tlb_index, const tlb_data& data) const {

    if (tlb_index < blackhole::TLB_COUNT_2M) {
        return data.apply_offset(blackhole::TLB_2M_OFFSET);
    } else {
        throw std::runtime_error("Invalid TLB index for Blackhole arch");
    }

}

    void* BlackholeTTDevice::get_reg_mapping(uint64_t byte_addr) {
        void *reg_mapping;
        if (pci_device->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
            byte_addr -= BAR0_BH_SIZE;
            reg_mapping = pci_device->bar4_wc;
        } else if (pci_device->system_reg_mapping != nullptr && byte_addr >= pci_device->system_reg_start_offset) {
            byte_addr -= pci_device->system_reg_offset_adjust;
            reg_mapping = pci_device->system_reg_mapping;
        } else if (pci_device->bar0_wc != pci_device->bar0_uc && byte_addr < pci_device->bar0_wc_size) {
            reg_mapping = pci_device->bar0_wc;
        } else {
            byte_addr -= pci_device->bar0_uc_offset;
            reg_mapping = pci_device->bar0_uc;
        }
        return reg_mapping;
    }

    void BlackholeTTDevice::write_block_through_tlb(uint64_t tlb_offset, uint32_t address, uint64_t tlb_size, uint32_t size_in_bytes, const uint8_t* buffer_addr) {
       
        if (pci_device->bar4_wc != nullptr && tlb_size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  write to DRAM (BAR4 space), we add offset
            // to which we write so write_block knows it needs to target BAR4
            write_block((tlb_offset + address % tlb_size) + BAR0_BH_SIZE, size_in_bytes, buffer_addr);
        } else {
            write_block(tlb_offset + address % tlb_size, size_in_bytes, buffer_addr);
        }
    }

    void BlackholeTTDevice::read_block_through_tlb(uint64_t tlb_offset, uint32_t address, uint64_t tlb_size, uint32_t size_in_bytes, uint8_t* buffer_addr) {
       
        if (pci_device->bar4_wc != nullptr && tlb_size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  read from DRAM (BAR4 space), we add offset
            // from which we read so read_block knows it needs to target BAR4
            read_block((tlb_offset + address % tlb_size) + BAR0_BH_SIZE, size_in_bytes, buffer_addr);
        } else {
            read_block(tlb_offset + address % tlb_size, size_in_bytes, buffer_addr);
        }
    }

    void BlackholeTTDevice::disable_atu() {
        // Disable ATU index 0
        // TODO: Implement disabling for all indexes, once more host channels are enabled.
        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;
        uint32_t region_ctrl_2 = 0 << 31;  // REGION_EN = 0
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(
                static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x04),
            &region_ctrl_2,
            1);
    }

    void BlackholeTTDevice::program_atu(uint32_t region_id_to_use, uint32_t region_size, uint64_t dest_addr) {
        uint32_t dest_bar_lo = dest_addr & 0xffffffff;
        uint32_t dest_bar_hi = (dest_addr >> 32) & 0xffffffff;
        uint64_t base_addr = region_id_to_use * region_size;
        uint64_t base_size = (region_id_to_use + 1) * region_size;
        uint64_t limit_address = base_addr + base_size - 1;

        uint32_t region_ctrl_1 = 1 << 13;  // INCREASE_REGION_SIZE = 1
        uint32_t region_ctrl_2 = 1 << 31;  // REGION_EN = 1
        uint32_t region_ctrl_3 = 0;
        uint32_t base_addr_lo = base_addr & 0xffffffff;
        uint32_t base_addr_hi = (base_addr >> 32) & 0xffffffff;
        uint32_t limit_address_lo = limit_address & 0xffffffff;
        uint32_t limit_address_hi = (limit_address >> 32) & 0xffffffff;

        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;

        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x00),
            &region_ctrl_1,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x04),
            &region_ctrl_2,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x08),
            &base_addr_lo,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x0c),
            &base_addr_hi,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x10),
            &limit_address_lo,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x14),
            &dest_bar_lo,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x18),
            &dest_bar_hi,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x1c),
            &region_ctrl_3,
            1);
        write_regs_nonclass(
            reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x20),
            &limit_address_hi,
            1);
    }

    SocDescriptor BlackholeTTDevice::get_soc_descriptor() {
        return SocDescriptor("soc_descriptors/blackhole_140_arch.yaml");
    }

}  // namespace tt::umd
