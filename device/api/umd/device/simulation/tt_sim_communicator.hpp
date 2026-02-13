// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>

namespace tt::umd {

/**
 * TTSimCommunicator handles low-level communication with the TTSim .so library.
 * It manages dynamic library loading, function pointer resolution, and provides
 * thread-safe access to simulator functions.
 *
 * This class can be used independently of TTSimTTDevice for direct simulator communication.
 */
class TTSimCommunicator final {
public:
    /**
     * Constructor for TTSimCommunicator.
     *
     * @param simulator_directory Path to the simulator binary/directory
     * @param copy_sim_binary If true, copy the simulator binary to memory for security
     */
    TTSimCommunicator(const std::filesystem::path &simulator_directory, bool copy_sim_binary = false);

    /**
     * Destructor that properly cleans up library handles and file descriptors.
     */
    ~TTSimCommunicator();

    /**
     * Initialize the simulator and establish communication.
     * Must be called before using any communication methods.
     * This loads the library, resolves function pointers, and starts the simulator.
     */
    void initialize();

    /**
     * Shutdown the simulator and close communication.
     */
    void shutdown();

    /**
     * Read data from a tile core.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to read from
     * @param data Buffer to store read data
     * @param size Number of bytes to read
     */
    void tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size);

    /**
     * Write data to a tile core.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to write to
     * @param data Data to write
     * @param size Number of bytes to write
     */
    void tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size);

    /**
     * Read data from PCI memory.
     *
     * @param paddr Physical address
     * @param data Buffer to store read data
     * @param size Number of bytes to read
     */
    void pci_mem_read_bytes(uint64_t paddr, void *data, uint32_t size);

    /**
     * Write data to PCI memory.
     *
     * @param paddr Physical address
     * @param data Data to write
     * @param size Number of bytes to write
     */
    void pci_mem_write_bytes(uint64_t paddr, const void *data, uint32_t size);

    /**
     * Read from PCI configuration space.
     *
     * @param bus_device_function Bus/device/function identifier
     * @param offset Offset in configuration space
     * @return 32-bit value read from configuration space
     */
    uint32_t pci_config_read32(uint32_t bus_device_function, uint32_t offset);

    /**
     * Advance the simulator clock.
     *
     * @param n_clocks Number of clock cycles to advance
     */
    void advance_clock(uint32_t n_clocks);

private:
    // Library management.
    void create_simulator_binary();
    off_t resize_simulator_binary(int src_fd);
    void copy_simulator_binary();
    void secure_simulator_binary();
    void close_simulator_binary();
    void load_simulator_library(const std::filesystem::path &path);

    // Dynamic library handle.
    void *libttsim_handle_ = nullptr;

    // File descriptor for copied simulator binary.
    int copied_simulator_fd_ = -1;

    // Simulator directory path.
    std::filesystem::path simulator_directory_;

    // Flag to indicate if binary should be copied to memory.
    bool copy_sim_binary_;

    // Function pointers to simulator library functions.
    void (*pfn_libttsim_init_)() = nullptr;
    void (*pfn_libttsim_exit_)() = nullptr;
    uint32_t (*pfn_libttsim_pci_config_rd32_)(uint32_t bus_device_function, uint32_t offset) = nullptr;
    void (*pfn_libttsim_pci_mem_rd_bytes_)(uint64_t paddr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_pci_mem_wr_bytes_)(uint64_t paddr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_rd_bytes_)(uint32_t x, uint32_t y, uint64_t addr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes_)(uint32_t x, uint32_t y, uint64_t addr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_clock_)(uint32_t n_clocks) = nullptr;

    // Thread safety.
    mutable std::mutex device_lock_;
};

}  // namespace tt::umd
