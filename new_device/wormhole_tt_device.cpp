// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "wormhole_tt_device.h"

namespace tt::umd {

std::tuple<xy_pair, xy_pair> WormholeTTDevice::multicast_workaround(xy_pair start, xy_pair end) const {
    // When multicasting there is a rare case where including the multicasting node in the box can result in a backup
    // and the multicasted data not reaching all endpoints specified. As a workaround we exclude the pci endpoint from
    // the multicast. This doesn't cause any problems with making some tensix cores inaccessible because column 0 (which
    // we are excluding) doesn't have tensix.
    start.x = start.x == 0 ? 1 : start.x;
    return std::make_tuple(start, end);
}

tlb_configuration WormholeTTDevice::get_tlb_configuration(uint32_t tlb_index) const {
    if (tlb_index >= wormhole::TLB_BASE_INDEX_16M) {
        return tlb_configuration{
            .size = wormhole::DYNAMIC_TLB_16M_SIZE,
            .base = wormhole::DYNAMIC_TLB_16M_BASE,
            .cfg_addr = wormhole::DYNAMIC_TLB_16M_CFG_ADDR,
            .index_offset = tlb_index - wormhole::TLB_BASE_INDEX_16M,
            .offset = wormhole::TLB_16M_OFFSET,
        };
    } else if (tlb_index >= wormhole::TLB_BASE_INDEX_2M) {
        return tlb_configuration{
            .size = wormhole::DYNAMIC_TLB_2M_SIZE,
            .base = wormhole::DYNAMIC_TLB_2M_BASE,
            .cfg_addr = wormhole::DYNAMIC_TLB_2M_CFG_ADDR,
            .index_offset = tlb_index - wormhole::TLB_BASE_INDEX_2M,
            .offset = wormhole::TLB_2M_OFFSET,
        };
    } else {
        return tlb_configuration{
            .size = wormhole::DYNAMIC_TLB_1M_SIZE,
            .base = wormhole::DYNAMIC_TLB_1M_BASE,
            .cfg_addr = wormhole::DYNAMIC_TLB_1M_CFG_ADDR,
            .index_offset = tlb_index - wormhole::TLB_BASE_INDEX_1M,
            .offset = wormhole::TLB_1M_OFFSET,
        };
    }
}

std::optional<std::tuple<std::uint64_t, std::uint64_t>> WormholeTTDevice::describe_tlb(std::int32_t tlb_index) const {
    std::uint32_t TLB_COUNT_1M = 156;
    std::uint32_t TLB_COUNT_2M = 10;
    std::uint32_t TLB_COUNT_16M = 20;

    std::uint32_t TLB_BASE_1M = 0;
    std::uint32_t TLB_BASE_2M = TLB_COUNT_1M * (1 << 20);
    std::uint32_t TLB_BASE_16M = TLB_BASE_2M + TLB_COUNT_2M * (1 << 21);
    if (tlb_index < 0) {
        return std::nullopt;
    }

    if (tlb_index >= 0 && tlb_index < TLB_COUNT_1M) {
        std::uint32_t size = 1 << 20;
        return std::tuple(TLB_BASE_1M + size * tlb_index, size);
    } else if (tlb_index >= 0 && tlb_index < TLB_COUNT_1M + TLB_COUNT_2M) {
        auto tlb_offset = tlb_index - TLB_COUNT_1M;
        auto size = 1 << 21;
        return std::tuple(TLB_BASE_2M + tlb_offset * size, size);
    } else if (tlb_index >= 0 and tlb_index < TLB_COUNT_1M + TLB_COUNT_2M + TLB_COUNT_16M) {
        auto tlb_offset = tlb_index - (TLB_COUNT_1M + TLB_COUNT_2M);
        auto size = 1 << 24;
        return std::tuple(TLB_BASE_16M + tlb_offset * size, size);
    }

    return std::nullopt;
}

std::pair<std::uint64_t, std::uint64_t> WormholeTTDevice::get_tlb_data(
    std::uint32_t tlb_index, const tlb_data &data) const {
    std::uint32_t TLB_COUNT_1M = 156;
    std::uint32_t TLB_COUNT_2M = 10;
    std::uint32_t TLB_COUNT_16M = 20;

    if (tlb_index < TLB_COUNT_1M) {
        return data.apply_offset(wormhole::TLB_1M_OFFSET);
    } else if (tlb_index < TLB_COUNT_1M + TLB_COUNT_2M) {
        return data.apply_offset(wormhole::TLB_2M_OFFSET);
    } else if (tlb_index < TLB_COUNT_1M + TLB_COUNT_2M + TLB_COUNT_16M) {
        return data.apply_offset(wormhole::TLB_16M_OFFSET);
    } else {
        throw std::runtime_error("Invalid TLB index for Wormhole arch");
    }
}

    void* WormholeTTDevice::get_reg_mapping(uint64_t byte_addr) {
        void *reg_mapping;
        // if (dev->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        //     byte_addr -= BAR0_BH_SIZE;
        //     reg_mapping = dev->bar4_wc;
        // } else 
        if (pci_device->system_reg_mapping != nullptr && byte_addr >= pci_device->system_reg_start_offset) {
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

    void WormholeTTDevice::write_block_through_tlb(uint64_t tlb_offset, uint32_t address, uint64_t tlb_size, uint32_t size_in_bytes, const uint8_t* buffer_addr) {
       
        // if (dev->bar4_wc != nullptr && tlb_size == BH_4GB_TLB_SIZE) {
        //     // This is only for Blackhole. If we want to  write to DRAM (BAR4 space), we add offset
        //     // to which we write so write_block knows it needs to target BAR4
        //     write_block(dev, (tlb_offset + address % tlb_size) + BAR0_BH_SIZE, size_in_bytes, buffer_addr);
        // } else {
            write_block(tlb_offset + address % tlb_size, size_in_bytes, buffer_addr);
        // }
    }

    void WormholeTTDevice::read_block_through_tlb(uint64_t tlb_offset, uint32_t address, uint64_t tlb_size, uint32_t size_in_bytes, uint8_t* buffer_addr) {
       
        // if (dev->bar4_wc != nullptr && tlb_size == BH_4GB_TLB_SIZE) {
        //     // This is only for Blackhole. If we want to  read from DRAM (BAR4 space), we add offset
        //     // from which we read so read_block knows it needs to target BAR4
        //     read_block(dev, (tlb_offset + address % tlb_size) + BAR0_BH_SIZE, size_in_bytes, buffer_addr);
        // } else {
            read_block(tlb_offset + address % tlb_size, size_in_bytes, buffer_addr);
        // }
    }

    void WormholeTTDevice::program_atu(uint32_t region_id_to_use, uint32_t region_size, uint64_t dest_addr) {

        uint32_t dest_bar_lo = dest_addr & 0xffffffff;
        uint32_t dest_bar_hi = (dest_addr >> 32) & 0xffffffff;
        bar_write32(get_arc_csm_mailbox_offset() + 0 * 4, region_id_to_use);
        bar_write32(get_arc_csm_mailbox_offset() + 1 * 4, dest_bar_lo);
        bar_write32(get_arc_csm_mailbox_offset() + 2 * 4, dest_bar_hi);
        bar_write32(get_arc_csm_mailbox_offset() + 3 * 4, region_size);
        pcie_arc_msg(
            0xaa00 | get_arc_message_setup_iatu_for_peer_to_peer(),
            true,
            0,
            0);
    }


    SocDescriptor WormholeTTDevice::get_soc_descriptor() {
        return SocDescriptor("soc_descriptors/wormhole_b0_80_arch.yaml");
    }

}  // namespace tt::umd
