// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>

#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * RtlSimCommunicator handles low-level communication with RTL simulation.
 * It manages the simulation host, subprocess spawning, and provides
 * thread-safe access to simulation functions via flatbuffer protocol.
 *
 * This class can be used independently of RtlSimulationChip for direct RTL simulator communication.
 */
class RtlSimCommunicator final {
public:
    /**
     * Constructor for RtlSimCommunicator.
     *
     * @param simulator_directory Path to the simulator binary/directory
     */
    explicit RtlSimCommunicator(const std::filesystem::path &simulator_directory);

    /**
     * Destructor that properly cleans up simulation host.
     */
    ~RtlSimCommunicator();

    /**
     * Initialize the simulator and establish communication.
     * Must be called before using any communication methods.
     * This spawns the simulator process and starts the host.
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
     * Assert reset for all Tensix cores.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     */
    void all_tensix_reset_assert(uint32_t x, uint32_t y);

    /**
     * Deassert reset for all Tensix cores.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     */
    void all_tensix_reset_deassert(uint32_t x, uint32_t y);

    /**
     * Assert reset for all NEO DM cores.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     */
    void all_neo_dms_reset_assert(uint32_t x, uint32_t y);

    /**
     * Deassert reset for all NEO DM cores.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     */
    void all_neo_dms_reset_deassert(uint32_t x, uint32_t y);

    /**
     * Assert reset for a specific NEO DM core.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param dm_index DM core index (0-7)
     */
    void neo_dm_reset_assert(uint32_t x, uint32_t y, uint32_t dm_index);

    /**
     * Deassert reset for a specific NEO DM core.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param dm_index DM core index (0-7)
     */
    void neo_dm_reset_deassert(uint32_t x, uint32_t y, uint32_t dm_index);

    /**
     * Get the simulation host reference.
     *
     * @return Reference to the SimulationHost
     */
    SimulationHost &get_host() { return host_; }

private:
    // Simulator directory path.
    std::filesystem::path simulator_directory_;

    // Simulation host for communication.
    SimulationHost host_;

    // Thread safety.
    mutable std::mutex device_lock_;
};

}  // namespace tt::umd
