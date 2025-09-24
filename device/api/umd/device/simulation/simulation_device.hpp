/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "umd/device/chip/chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

class SimulationDevice : public Chip {
public:
    static std::string get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path);

    SimulationDevice(const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor);
    ~SimulationDevice();

    SimulationHost host;

    int get_num_host_channels() override;
    int get_host_channel_size(std::uint32_t channel) override;
    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    void start_device() override;
    void close_device() override;

    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    bool is_mmio_capable() const override { return false; }

    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) override;

    // All tt_xy_pair cores in this class are defined in VIRTUAL coords.
    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;
    void write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) override;
    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;

    std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable() override;

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    void send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) override;
    void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) override;
    void deassert_risc_resets() override;

    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;

    void set_power_state(DevicePowerState state) override;
    int get_clock() override;
    int get_numa_node() override;

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        uint32_t timeout_ms = 1000,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) override;

private:
    void send_tensix_risc_reset(tt_xy_pair core, const TensixSoftResetOptions& soft_resets);

    // State variables
    driver_noc_params noc_params;
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    tt::ARCH arch_name;
    std::shared_ptr<ClusterDescriptor> cluster_descriptor;
    std::unordered_map<chip_id_t, SocDescriptor> soc_descriptor_per_chip = {};

    // To enable DPRINT usage in the Simulator,
    // the simulation device code should acquire a lock
    // to ensure it can be called safely from multiple threads.
    std::mutex device_lock;

    void* libttsim_handle = nullptr;
    void (*pfn_libttsim_init)() = nullptr;
    void (*pfn_libttsim_exit)() = nullptr;
    void (*pfn_libttsim_tile_rd_bytes)(uint32_t x, uint32_t y, uint64_t addr, void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes)(uint32_t x, uint32_t y, uint64_t addr, const void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tensix_reset_deassert)(uint32_t x, uint32_t y) = nullptr;
    void (*pfn_libttsim_tensix_reset_assert)(uint32_t x, uint32_t y) = nullptr;
    void (*pfn_libttsim_clock)(uint32_t n_clocks) = nullptr;
};

}  // namespace tt::umd
