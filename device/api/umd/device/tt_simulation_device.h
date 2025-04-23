/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "umd/device/cluster.h"
#include "umd/device/tt_simulation_host.hpp"

class tt_SimulationDeviceInit {
public:
    tt_SimulationDeviceInit(const std::filesystem::path& simulator_directory);

    tt::ARCH get_arch_name() const { return soc_descriptor.arch; }

    const tt_SocDescriptor& get_soc_descriptor() const { return soc_descriptor; }

    std::filesystem::path get_simulator_path() const { return simulator_directory / "run.sh"; }

private:
    std::filesystem::path simulator_directory;
    tt_SocDescriptor soc_descriptor;
};

class tt_SimulationDevice : public tt_device {
public:
    tt_SimulationDevice(const std::filesystem::path& simulator_directory) :
        tt_SimulationDevice(tt_SimulationDeviceInit(simulator_directory)) {}

    tt_SimulationDevice(const tt_SimulationDeviceInit& init);
    ~tt_SimulationDevice();

    tt_SimulationHost host;

    virtual void set_barrier_address_params(const barrier_address_params& barrier_address_params_);
    virtual void start_device(const tt_device_params& device_params);
    virtual void assert_risc_reset();
    virtual void deassert_risc_reset();

    virtual void deassert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET);
    virtual void assert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET);

    virtual void close_device();

    // Runtime Functions
    virtual void write_to_device(const void* mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr);
    virtual void write_to_device(
        const void* mem_ptr, uint32_t size_in_bytes, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr);
    virtual void read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size);
    virtual void read_from_device(void* mem_ptr, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr, uint32_t size);

    void dma_write_to_device(const void* src, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) {
        throw std::runtime_error("DMA write not supported in simulation mode.");
    }

    void dma_read_from_device(void* dst, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) {
        throw std::runtime_error("DMA read not supported in simulation mode.");
    }

    virtual void wait_for_non_mmio_flush();
    virtual void wait_for_non_mmio_flush(const chip_id_t chip);
    void l1_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores = {});
    void dram_membar(const chip_id_t chip, const std::unordered_set<uint32_t>& channels);
    void dram_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores = {});

    // Misc. Functions to Query/Set Device State
    static std::vector<chip_id_t> detect_available_device_ids();
    virtual std::set<chip_id_t> get_target_device_ids();
    virtual std::set<chip_id_t> get_target_mmio_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual std::map<int, int> get_clocks();
    virtual void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;
    virtual std::uint64_t get_pcie_base_addr_from_device(const chip_id_t chip_id) const;
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id);
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id);
    virtual const tt_SocDescriptor& get_soc_descriptor(chip_id_t chip_id) const;

    virtual void configure_active_ethernet_cores_for_mmio_device(
        chip_id_t mmio_chip, const std::unordered_set<tt::umd::CoreCoord>& active_eth_cores_per_chip);

    tt_xy_pair translate_to_api_coords(const chip_id_t chip, const tt::umd::CoreCoord core_coord) const;

private:
    // State variables
    tt_driver_noc_params noc_params;
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    tt::ARCH arch_name;
    std::shared_ptr<tt_ClusterDescriptor> cluster_descriptor;
    std::unordered_map<chip_id_t, tt_SocDescriptor> soc_descriptor_per_chip = {};
};
