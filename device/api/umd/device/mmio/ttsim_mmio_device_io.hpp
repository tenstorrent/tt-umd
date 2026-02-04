// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "umd/device/mmio/mmio_device_io.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt::umd {

/**
 * TTSim implementation of MMIODeviceIO that interfaces with the TTSim simulator.
 * Similar to TTSimTTDevice but focused on MMIO operations.
 */
class TTSimMMIODeviceIO : public MMIODeviceIO {
public:
    /**
     * Constructor for TTSim MMIO device.
     *
     * @param simulator_directory Path to the TTSim simulator directory.
     * @param soc_descriptor SoC descriptor for the device.
     * @param size Size of the memory window.
     * @param base_address Base address for the window.
     * @param config Initial TLB configuration.
     */
    TTSimMMIODeviceIO(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        size_t size,
        uint64_t base_address = 0,
        const tlb_data& config = {});

    ~TTSimMMIODeviceIO() override;

    void write32(uint64_t offset, uint32_t value) override;

    uint32_t read32(uint64_t offset) override;

    void write_register(uint64_t offset, const void* data, size_t size) override;

    void read_register(uint64_t offset, void* data, size_t size) override;

    void write_block(uint64_t offset, const void* data, size_t size) override;

    void read_block(uint64_t offset, void* data, size_t size) override;

    void read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) override;

    void write_block_reconfigure(
        const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) override;

    void noc_multicast_write_reconfigure(
        void* dst,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        uint64_t ordering = tlb_data::Strict) override;

    size_t get_size() const override;

    void configure(const tlb_data& new_config) override;

    uint64_t get_base_address() const override;

    /**
     * Initialize the TTSim simulator library.
     */
    void initialize_simulator();

    /**
     * Close the TTSim simulator.
     */
    void close_simulator();

protected:
    void validate(uint64_t offset, size_t size) const override;

private:
    void load_simulator_library(const std::filesystem::path& path);
    void create_simulator_binary();
    void close_simulator_binary();

    // TTSim function pointers (similar to TTSimTTDevice)
    void* libttsim_handle = nullptr;
    void (*pfn_libttsim_init)() = nullptr;
    void (*pfn_libttsim_exit)() = nullptr;
    uint32_t (*pfn_libttsim_pci_config_rd32)(uint32_t bus_device_function, uint32_t offset) = nullptr;
    void (*pfn_libttsim_pci_mem_rd_bytes)(uint64_t paddr, void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_pci_mem_wr_bytes)(uint64_t paddr, const void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_rd_bytes)(uint32_t x, uint32_t y, uint64_t addr, void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes)(uint32_t x, uint32_t y, uint64_t addr, const void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_clock)(uint32_t n_clocks) = nullptr;

    std::mutex device_lock_;
    std::filesystem::path simulator_directory_;
    SocDescriptor soc_descriptor_;
    uint64_t base_address_;
    tlb_data config_;
    size_t window_size_;
    int copied_simulator_fd_ = -1;
    bool simulator_initialized_ = false;

    /**
     * Convert core coordinates and address to physical address for TTSim.
     */
    uint64_t translate_address_for_ttsim(tt_xy_pair core, uint64_t addr) const;
};

}  // namespace tt::umd