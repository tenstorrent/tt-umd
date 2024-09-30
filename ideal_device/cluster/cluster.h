/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "common_types.h"
#include "cluster_descriptor.h"
#include "chip.h"

#include <functional>
#include <unordered_set>


namespace tt::umd {

class Cluster {
    public:
    // Without arguments it will open whole cluster.
    Cluster();
    Cluster(std::unordered_set<chip_id_t> chips_to_open);

    // This is redundant, but sure why not.
    ClusterDescriptor* get_cluster_descriptor();
    
    // By getting any chip in cluster, you can then call functions over it.
    // Cluster copies interface for chip and offers convenience calls for the whole cluster.
    Chip* get_chip(chip_id_t chip_id);
    std::unordered_map<chip_id_t, Chip*> get_chips();

    // A flexible way to offer chip and core interface on cluster level, run any function defined in Chip class on a set of chips;
    void run_on_chips(std::function<void(Chip*)> func, std::unordered_set<chip_id_t> chips);
    // Runs on all chips.
    void run_on_chips(std::function<void(Chip*)> func);
    // 
    template <typename T>
    std::vector<T> run_on_chips(std::function<T(Chip*)> func);
    // examples
    // cluster->run_on_chips([&](Chip* chip) { chip->wait_for_non_mmio_flush() }, mmio_chips);
    // cluster->run_on_chips([&](Chip* chip) { chip->run_on_cores([&](Core* core) { core->write_to_device(mem_ptr, size_in_bytes, addr); }, chip->get_worker_cores()); }, all_chips);

    // Same interface from Chip, but done for the whole cluster.
    void set_device_l1_address_params(const device_l1_address_params& l1_address_params_);
    void set_device_dram_address_params(const device_dram_address_params& dram_address_params_);
    void set_driver_host_address_params(const driver_host_address_params& host_address_params_);
    void set_driver_eth_interface_params(const driver_eth_interface_params& eth_interface_params_);

    void start_cluster(const device_params& device_params);
    void close_device();
    void deassert_risc_reset();
    void assert_risc_reset();

    // write and read functions are implemented like this for cores:
    // cluster->get_chip(chip_id)->get_core(x, y)->write_to_device(mem_ptr, size_in_bytes, addr);\
    //
    // and like this for chip:
    // cluster->get_chip(chip_id)->write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel);

    // sync functions are the same
    // cluster->get_chip(chip_id)->run_on_cores([&](Core* core) { core->l1_membar(); }, chip->get_worker_cores());
    // cluster->get_chip(chip_id)->wait_for_non_mmio_flush();

    // See if this is something that can be moved per chip.
    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude,
        const std::string& fallback_tlb);

    std::map<int, int> get_clocks();

    // has to be the same for the whole cluster (verified on startup), so only per cluster
    tt_version get_ethernet_fw_version();
};

}  // namespace tt::umd