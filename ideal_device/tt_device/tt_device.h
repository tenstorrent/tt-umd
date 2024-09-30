/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "pci/pci_device.h"
#include "tlb.h"
#include "chip/soc_descriptor.h"

#include <optional>
#include <memory>
#include <unordered_map>
#include <set>

namespace tt::umd {

struct dynamic_tlb {
    uint64_t bar_offset;      // Offset that address is mapped to, within the PCI BAR.
    uint64_t remaining_size;  // Bytes remaining between bar_offset and end of the TLB.
};

// Layer above PCIDevice.
// Wraps up all device type specifics.
// This is virtual class, with overridden implementations for GS WH BH.
// This is still tied to a local chip only.
// This class should be enough for all interaction with the local device you'd want to have, and it should not be encouraged to get pci_device.
class TTDevice {
   protected:
    TTDevice(std::unique_ptr<PCIDevice> pci_device);

   public:

    // Opens a PCIDevice for usage. Wraps it up inside a architecture specific TTDevice.
    // Also uses cluster map to get harvesting info and have proper soc_descriptor internally.
    static std::unique_ptr<TTDevice> open(unsigned int device_id);
    std::unique_ptr<PCIDevice> pci_device;
    
    // TT Device has it's soc descriptor, specific for this arch and harvesting info.
    virtual SocDescriptor get_soc_descriptor() = 0;
    
    // general device related stuff
    bool is_hardware_hung();
    bool auto_reset_board();
    void detect_ffffffff_read(std::uint32_t data_read = 0xffffffffu);
    virtual void program_atu(uint32_t region_id_to_use, uint32_t region_size, uint64_t dest_addr) = 0;
    virtual void disable_atu();
    int pcie_arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);

    // tlb related
    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index,
        physical_coord start,
        physical_coord end,
        std::uint64_t address,
        bool multicast,
        // std::unordered_map<chip_id_t, std::unordered_map<xy_pair, xy_pair>>& harvested_coord_translation,
        std::uint64_t ordering);
    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index,
        physical_coord target,
        std::uint64_t address,
        // std::unordered_map<chip_id_t, std::unordered_map<xy_pair, xy_pair>>& harvested_coord_translation,
        std::uint64_t ordering = tlb_data::Relaxed);
    dynamic_tlb set_dynamic_tlb_broadcast(
        unsigned int tlb_index,
        std::uint64_t address,
        // std::unordered_map<chip_id_t, std::unordered_map<xy_pair, xy_pair>>& harvested_coord_translation,
        physical_coord start,
        physical_coord end,
        std::uint64_t ordering = tlb_data::Relaxed);

    // read/write related
    // Could be that not all are needed. We may need just to read/write through TLBs, but chip/cores would have tlb pointers already.
    void read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t *buffer_addr);
    void write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t *buffer_addr);
    virtual void write_block_through_tlb(uint64_t tlb_offset, uint32_t address, uint64_t tlb_size, uint32_t size_in_bytes, const uint8_t* buffer_addr) = 0;
    virtual void read_block_through_tlb(uint64_t tlb_offset, uint32_t address, uint64_t tlb_size, uint32_t size_in_bytes, uint8_t* buffer_addr) = 0;
    void write_regs(uint32_t byte_addr, uint32_t word_len, const void *data);
    void write_tlb_reg(
        uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size);
    void read_regs(uint32_t byte_addr, uint32_t word_len, void *data);
    virtual void* get_reg_mapping(uint64_t byte_addr) = 0;


    // These should be pruned down. Some might make more sense being in SoC descriptor.
    // Mostly taken for "architecture_implementation.h"
    virtual uint32_t get_arc_message_arc_get_harvesting() const = 0;
    virtual uint32_t get_arc_message_arc_go_busy() const = 0;
    virtual uint32_t get_arc_message_arc_go_long_idle() const = 0;
    virtual uint32_t get_arc_message_arc_go_short_idle() const = 0;
    virtual uint32_t get_arc_message_deassert_riscv_reset() const = 0;
    virtual uint32_t get_arc_message_get_aiclk() const = 0;
    virtual uint32_t get_arc_message_setup_iatu_for_peer_to_peer() const = 0;
    virtual uint32_t get_arc_message_test() const = 0;
    virtual uint32_t get_arc_csm_mailbox_offset() const = 0;
    virtual uint32_t get_arc_reset_arc_misc_cntl_offset() const = 0;
    virtual uint32_t get_arc_reset_scratch_offset() const = 0;
    virtual uint32_t get_dram_channel_0_peer2peer_region_start() const = 0;
    virtual uint32_t get_dram_channel_0_x() const = 0;
    virtual uint32_t get_dram_channel_0_y() const = 0;
    virtual uint32_t get_broadcast_tlb_index() const = 0;
    virtual uint32_t get_dynamic_tlb_2m_base() const = 0;
    virtual uint32_t get_dynamic_tlb_2m_size() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_base() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_size() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_cfg_addr() const = 0;
    virtual uint32_t get_mem_large_read_tlb() const = 0;
    virtual uint32_t get_mem_large_write_tlb() const = 0;
    virtual uint32_t get_static_tlb_cfg_addr() const = 0;
    virtual uint32_t get_static_tlb_size() const = 0;
    virtual uint32_t get_reg_tlb() const = 0;
    virtual uint32_t get_tlb_base_index_16m() const = 0;
    virtual uint32_t get_tensix_soft_reset_addr() const = 0;
    virtual uint32_t get_grid_size_x() const = 0;
    virtual uint32_t get_grid_size_y() const = 0;
    virtual uint32_t get_tlb_cfg_reg_size_bytes() const = 0;
    // Replace with std::span once we enable C++20
    virtual const std::vector<uint32_t>& get_harvesting_noc_locations() const = 0;
    virtual const std::vector<uint32_t>& get_t6_x_locations() const = 0;
    virtual const std::vector<uint32_t>& get_t6_y_locations() const = 0;

    virtual std::tuple<tt_xy_pair, tt_xy_pair> multicast_workaround(tt_xy_pair start, tt_xy_pair end) const = 0;
    virtual tlb_configuration get_tlb_configuration(uint32_t tlb_index) const = 0;
    virtual std::optional<std::tuple<std::uint64_t, std::uint64_t>> describe_tlb(std::int32_t tlb_index) const = 0;
    virtual std::pair<std::uint64_t, std::uint64_t> get_tlb_data(
        std::uint32_t tlb_index, const tlb_data& data) const = 0;

    void print_device_info();

    bool tensix_or_eth_in_broadcast(const std::set<uint32_t>& cols_to_exclude);
    bool valid_tensix_broadcast_grid(
        const std::set<uint32_t>& rows_to_exclude,
        const std::set<uint32_t>& cols_to_exclude);


};

}  // namespace tt::umd