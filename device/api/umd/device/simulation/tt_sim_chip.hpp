// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>

#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"

// static void (*s_pfn_libttsim_pci_dma_mem_rd_bytes)(uint64_t paddr, void *p, uint32_t size);
// static void (*s_pfn_libttsim_pci_dma_mem_wr_bytes)(uint64_t paddr, const void *p, uint32_t size);
// #if TT_VERSION == 0
// static uint64_t s_tlb_cfg[186];
// #elif TT_VERSION == 1
// static uint32_t s_tlb_cfg[210*3];
// #endif

// extern "C" API_EXPORT void libttsim_init() {
//     TTSIM_VERIFY(!s_ttsim_running, ConfigurationError, "sim is already running");
//     ttsim_init();
//     s_ttsim_running = true;
// }

// extern "C" API_EXPORT void libttsim_exit() {
//     TTSIM_VERIFY(s_ttsim_running, ConfigurationError, "sim is not running");
//     ttsim_exit();
//     s_ttsim_running = false;
// }

// extern "C" API_EXPORT void libttsim_set_pci_dma_mem_callbacks(
//     decltype(s_pfn_libttsim_pci_dma_mem_rd_bytes) pfn_libttsim_pci_dma_mem_rd_bytes,
//     decltype(s_pfn_libttsim_pci_dma_mem_wr_bytes) pfn_libttsim_pci_dma_mem_wr_bytes
// ) {
//     TTSIM_VERIFY(!s_ttsim_running, ConfigurationError, "sim is already running");
//     s_pfn_libttsim_pci_dma_mem_rd_bytes = pfn_libttsim_pci_dma_mem_rd_bytes;
//     s_pfn_libttsim_pci_dma_mem_wr_bytes = pfn_libttsim_pci_dma_mem_wr_bytes;
// }

namespace tt::umd {

// TTSIM implementation using dynamic library (.so files).
class TTSimChip : public SimulationChip {
public:
    TTSimChip(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        bool copy_sim_binary = false,
        int num_host_mem_channels = 0);
    ~TTSimChip() override;

    void start_device() override;
    void close_device() override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;

    void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) override;
    void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) override;
    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;

private:
    void create_simulator_binary();
    off_t resize_simulator_binary(int src_fd);
    void copy_simulator_binary();
    void secure_simulator_binary();
    void close_simulator_binary();
    void load_simulator_library(const std::filesystem::path& path);
    void initialize_sysmem_functions();

    void pci_dma_read_bytes(uint64_t paddr, void* p, uint32_t size);
    void pci_dma_write_bytes(uint64_t paddr, const void* p, uint32_t size);

    std::unique_ptr<TTSimTTDevice> tt_device_;
};

}  // namespace tt::umd
