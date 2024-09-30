/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "core/core.h"
#include "chip/soc_descriptor.h"
#include "io/abstract_io.h"
#include "tt_device/tt_device.h"

#include <cstdint>
#include <functional>
#include <unordered_set>

namespace tt::umd {

enum class ChipType {
    Local,
    Remote,
    Versim,
    Mock
};

enum class tlb_type {
    tlb_1m,
    tlb_2m,
    tlb_16m,
    tlb_4gb,
};

struct tlb_index {
    tlb_type type;
    int index;
}

// This is a layer which should be used by a regular user.
// This hides implementation details for local, remote, versim, and mock chips.
// Unless you want to do something very specific, you should not have a need to take underlying TTDevice, which will only be present in local chip
// All arch specific stuff is hidden in specific TTDevice implementation.
class Chip {
public:
    // Every chip has its matching id from cluster descriptor
    Chip(uint32_t device_id);

    virtual ChipType get_chip_type();

    // Gets you core class which itself has some specifics.
    virtual Core* get_core(physical_coord core);
    // Gets you a DRAM core. You could also get_core(get_soc_descriptor->get_dram_core()).
    virtual Core* get_dram_core(uint32_t dram_channel);

    // A flexible way to offer core interface on chip level, run any function defined in Core class on a set of cores;
    void run_on_cores(std::function<void(Core*)> func, std::unordered_set<tt_xy_pair> cores);
    // Run on all cores
    void run_on_cores(std::function<void(Core*)> func);
    // Run with return value.
    template <typename T>
    std::vector<T> run_on_cores(std::function<T(Core*)> func);

    // Returns the descriptor of the underlying chip.
    virtual SocDescriptor get_soc_descriptor();

    // Sets some parameters which are needed for the chip to work.
    // There parameters are to be re-thinked. Ones which are hardware related should live in UMD and not passed by client
    virtual void set_device_l1_address_params(const device_l1_address_params& l1_address_params_);
    virtual void set_device_dram_address_params(const device_dram_address_params& dram_address_params_);
    virtual void set_driver_host_address_params(const driver_host_address_params& host_address_params_);
    virtual void set_driver_eth_interface_params(const driver_eth_interface_params& eth_interface_params_);

    // Starts the device after which you can start using it. Will internally create all cores. TLB maps are locked at this point.
    virtual void start_device(const device_params& device_params);
    virtual void close_device();

    // Read write to system memory.
    // Available for all chips, but num channels for remote chip will be zero.
    virtual void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel);
    virtual void write_to_sysmem(const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel);
    virtual void read_from_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, uint32_t size);
    virtual void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size);

    // Returns an IO object which can be used for fast read/write to system memory.
    // Can start at some address conveniently and have a size limit (that is not allowed to go out of original scope).
    virtual std::unique_ptr<AbstractIO> get_sysmem_io(uint16_t channel, uint64_t base_addr = 0, uint64_t size = 0);

    // different membars/flushes
    // non_mmio_flush does something only on remote chip.
    // This one might be unnecessary, but it is used currently by tt-metal, but every time they write to core, they call this? That's why it might not be needed.
    virtual void wait_for_non_mmio_flush() {};
    // The remote chip's l1 and dram membars are implemented as wait_for_non_mmio_flush.
    // For local chip, this will have some implemention.
    // They have to be defined here, since depending on the chip/core type (if remote specifically), they could call a chip wide function.
    void l1_membar(physical_coord core);
    void dram_membar(physical_coord core);

    // Copied from Core interface, done for all cores.
    virtual void deassert_risc_reset();
    virtual void assert_risc_reset();

    // Double check if this makes sense only for local chip.
    // Maybe remote chip just returns local one's.
    virtual int get_clock();
    virtual std::uint32_t get_numa_node();
    virtual std::uint64_t get_pcie_base_addr_from_device();

    // 0 for remote chip
    virtual std::uint32_t get_num_host_channels();
    virtual std::uint32_t get_host_channel_size(std::uint32_t channel);

    // Throws for remote chip.
    virtual void configure_active_ethernet_cores_for_mmio_device(
        const std::unordered_set<physical_coord>& active_eth_cores_per_chip);
    
    // Also throws for remote chip.
    // TLB setup is done by default, and is hidden behind chip implementation.
    // If you want to have your own TLB setup, you have to grab the TTDevice and do it there.
    // After that you setup the core to tlb mapping here.
    // This all has to be done before you start using the chip (start_device), or it will fail.
    virtual void setup_core_to_tlb_map(std::unordered_map<tlb_index, physical_coord> mapping_function);

    // Also throws for remote chip, since there is no such thing.
    virtual void TTDevice* get_tt_device();

    // Throws for local chip since only remote chip has underlying localchip.
    virtual Chip* get_local_chip();
};

}