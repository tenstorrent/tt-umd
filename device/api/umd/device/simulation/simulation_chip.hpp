// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

#include "umd/device/chip/chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

// Base class for all simulation devices.
class SimulationChip : public Chip {
public:
    static std::string get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path);

    static std::unique_ptr<SimulationChip> create(
        const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id);

    virtual ~SimulationChip() = default;

    // Common interface methods - most have simple implementations.
    int get_num_host_channels() override;
    int get_host_channel_size(std::uint32_t channel) override;
    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    bool is_mmio_capable() const override { return false; }

    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) override;

    void write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) override;
    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;
    void noc_multicast_write(void* dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    void send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) override;
    void deassert_risc_resets() override;

    void set_power_state(DevicePowerState state) override;
    int get_clock() override;
    int get_numa_node() override;

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) override;

    // Pure virtual methods that derived classes must implement.
    virtual void start_device() override = 0;
    virtual void close_device() override = 0;

    // All tt_xy_pair cores in this class are defined in VIRTUAL coords.
    virtual void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override = 0;
    virtual void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override = 0;

    virtual void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) = 0;
    virtual void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) override = 0;
    virtual void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override = 0;
    virtual void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override = 0;

protected:
    SimulationChip(const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id);

    // Simulator directory.
    // Common state variables.
    DriverNocParams noc_params;
    tt::ARCH arch_name;
    ChipId chip_id_;
    std::shared_ptr<ClusterDescriptor> cluster_descriptor;

    // To enable DPRINT usage in the Simulator,
    // the simulation device code should acquire a lock
    // to ensure it can be called safely from multiple threads.
    std::mutex device_lock;

    std::filesystem::path simulator_directory_;
};
}  // namespace tt::umd
