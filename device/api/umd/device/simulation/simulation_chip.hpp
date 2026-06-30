// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "umd/device/chip/chip.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {
class ClusterDescriptor;
class SocDescriptor;

// Concrete simulation chip. Backend variance (TTSim .so vs RTL subprocess) lives
// in the held TTDevice subtype, not in this class.
class SimulationChip : public Chip {
public:
    static std::string get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path);
    // An optional cluster_descriptor.yaml beside the simulator describes a (possibly multichip) topology.
    // Returns the path where it would live; the file is not required to exist.
    static std::string get_cluster_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path);

    static std::unique_ptr<SimulationChip> create(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        size_t num_chips,
        int num_host_mem_channels = 0);

    SimulationChip(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        std::unique_ptr<TTDevice> tt_device);

    ~SimulationChip() override = default;

    const SocDescriptor& get_soc_descriptor() const override { return tt_device_->get_soc_descriptor(); }

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
    void dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel = 0) override;

    void deassert_risc_resets() override;

    void set_power_state(DevicePowerState state) override;
    int get_clock() override;
    int get_numa_node() override;

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) override;

    void start_device(uint32_t dram_membar_subchannel = 0) override;
    void close_device() override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) override;

    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;

protected:
    // Common state variables.
    DriverNocParams noc_params;
    tt::ARCH arch_name;
    ChipId chip_id_;
    std::shared_ptr<ClusterDescriptor> cluster_descriptor;

    // DPRINT support: the simulation device code should acquire a lock
    // to ensure it can be called safely from multiple threads.
    std::mutex device_lock;

    std::filesystem::path simulator_directory_;

    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<TLBManager> tlb_manager_;
};
}  // namespace tt::umd
