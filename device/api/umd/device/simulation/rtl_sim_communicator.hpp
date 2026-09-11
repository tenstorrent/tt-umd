// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

#include "umd/device/simulation/simulation_host.hpp"

namespace tt::umd {

/**
 * RtlSimCommunicator handles low-level communication with RTL simulation.
 * It manages the simulation host, subprocess spawning, and provides
 * thread-safe access to simulation functions via flatbuffer protocol.
 *
 * This class can be used independently of SimulationChip for direct RTL simulator communication.
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
     * Read data from a tile core via SMN.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to read from
     * @param data Buffer to store read data
     * @param size Number of bytes to read
     */
    void smn_tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size);

    /**
     * Write data to a tile core via SMN.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to write to
     * @param data Data to write
     * @param size Number of bytes to write
     */
    void smn_tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size);

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
     * Assert uncore reset for all NEO DM cores across all tensix cores.
     */
    void all_neo_dms_uncore_reset_assert();

    /**
     * Deassert uncore reset for all NEO DM cores across all tensix cores.
     */
    void all_neo_dms_uncore_reset_deassert();

    /**
     * Assert uncore reset for NEO DM cores on a specific tensix core.
     *
     * @param x Core X coordinate.
     * @param y Core Y coordinate.
     */
    void neo_dm_uncore_reset_assert(uint32_t x, uint32_t y);

    /**
     * Deassert uncore reset for NEO DM cores on a specific tensix core.
     *
     * @param x Core X coordinate.
     * @param y Core Y coordinate.
     */
    void neo_dm_uncore_reset_deassert(uint32_t x, uint32_t y);

    /**
     * Get the simulation host reference.
     *
     * @return Reference to the SimulationHost
     */
    SimulationHost &get_host() { return host_; }

    // Callback for AXI RAM write: (address, data, size) -> write data into host memory.
    using RamWriteCallback = std::function<void(uint64_t address, const void *data, uint32_t size)>;

    // Callback for AXI RAM read: (address, data_out, size) -> read data from host memory into data_out.
    using RamReadCallback = std::function<void(uint64_t address, void *data_out, uint32_t size)>;

    /**
     * Set callbacks for AXI RAM notifications from the simulator.
     * These allow the caller to handle device-initiated reads/writes to host system memory.
     *
     * @param write_cb Called when the device writes to host RAM.
     * @param read_cb Called when the device reads from host RAM.
     */
    void set_ram_callbacks(RamWriteCallback write_cb, RamReadCallback read_cb);

    // Structure for received messages queued by the notification thread.
    struct ReceivedMessage {
        void *data = nullptr;
        size_t size = 0;
    };

private:
    // Notification handler thread entry point.
    // This thread is started with the simulation host, and receives all communication from the simulator.
    // In its implementation, it will handle (call the right callbacks) the simulator initiated
    // communication, like receiving host ram IO requests.
    // If the received message is, in fact, a response to a previously send command from the simulator host,
    // like a read request, it will just put it into command_queue, to be consumed by the operation waiting on a
    // response.
    void notification_handler_thread();

    // Wait for a regular command response from the command queue.
    // Command queue is filled up by notification_handler_thread. Operations that need to wait
    // on a response from the simulator should call this function to consume a response from the
    // said command queue.
    ReceivedMessage wait_for_command_response();

    // Handle AXI RAM write notification from the simulator.
    void handle_ram_write_notification(const void *notification);

    // Handle AXI RAM read notification from the simulator.
    void handle_ram_read_notification(const void *notification);

    // Simulator directory path.
    std::filesystem::path simulator_directory_;

    // Simulation host for communication.
    SimulationHost host_;

    // Thread safety for send operations.
    mutable std::mutex device_lock_;

    // Notification handler thread.
    std::thread notification_thread_;
    std::atomic<bool> notification_thread_running_{false};

    // Queue for regular command responses (non-notification messages).
    std::queue<ReceivedMessage> command_queue_;
    std::mutex command_queue_mutex_;
    std::condition_variable command_queue_cv_;

    // AXI RAM callbacks.
    RamWriteCallback ram_write_callback_;
    RamReadCallback ram_read_callback_;
};

}  // namespace tt::umd
