// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"

namespace tt::umd {
class TTSimTTDevice : public TTDevice {
public:
    TTSimTTDevice(
        const std::filesystem::path &simulator_directory,
        SocDescriptor soc_descriptor,
        ChipId chip_id,
        bool copy_sim_binary = false);
    ~TTSimTTDevice();

    static std::unique_ptr<TTSimTTDevice> create(const std::filesystem::path &simulator_directory);

    void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void send_tensix_risc_reset(tt_xy_pair translated_core, bool deassert);

    SocDescriptor *get_soc_descriptor() { return &soc_descriptor_; }

    bool is_hardware_hung() override { return false; }

    void dma_d2h(void *dst, uint32_t src, size_t size) override;
    void dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) override;
    void dma_h2d(uint32_t dst, const void *src, size_t size) override;
    void dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) override;
    void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    bool wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) override;
    std::chrono::milliseconds wait_eth_core_training(
        const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) override;
    EthTrainStatus read_eth_core_training_status(tt_xy_pair eth_core) override;
    uint32_t get_clock() override;
    uint32_t get_min_clock_freq() override;
    bool get_noc_translation_enabled() override;
    void dma_multicast_write(
        void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

    void close_device();
    void start_device();

    void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions &soft_resets);
    void send_tensix_risc_reset(const TensixSoftResetOptions &soft_resets);
    void assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs);
    void deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start);

private:
    void create_simulator_binary();
    off_t resize_simulator_binary(int src_fd);
    void copy_simulator_binary();
    void secure_simulator_binary();
    void close_simulator_binary();
    void load_simulator_library(const std::filesystem::path &path);

    void *libttsim_handle = nullptr;
    uint32_t libttsim_pci_device_id = 0;
    void (*pfn_libttsim_init)() = nullptr;
    void (*pfn_libttsim_exit)() = nullptr;
    uint32_t (*pfn_libttsim_pci_config_rd32)(uint32_t bus_device_function, uint32_t offset) = nullptr;
    void (*pfn_libttsim_pci_mem_rd_bytes)(uint64_t paddr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_pci_mem_wr_bytes)(uint64_t paddr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_rd_bytes)(uint32_t x, uint32_t y, uint64_t addr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes)(uint32_t x, uint32_t y, uint64_t addr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_clock)(uint32_t n_clocks) = nullptr;
    uint32_t tlb_region_size = 0;

    std::mutex device_lock;
    std::filesystem::path simulator_directory_;
    SocDescriptor soc_descriptor_;
    ChipId chip_id_;
    std::unique_ptr<architecture_implementation> architecture_impl_;
    int copied_simulator_fd_ = -1;
};
}  // namespace tt::umd
