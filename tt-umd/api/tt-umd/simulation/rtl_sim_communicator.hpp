// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "tt-umd/simulation/rtl_sim_session.hpp"
#include "tt-umd/simulation/simulation_host.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

class RtlSimChannel;

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
     * Communicate over @p host, one socket of an RtlSimSession that has already been started, on a
     * channel of its own. initialize() then only starts the notification thread; the caller keeps the
     * session alive.
     */
    explicit RtlSimCommunicator(SimulationHost &host);

    /**
     * Communicate over @p socket, on the channel its session keeps for it: communicators on the same
     * socket share it, and take turns with their requests. Keeps the session alive.
     */
    explicit RtlSimCommunicator(const RtlSimSocket &socket);

    /**
     * Destructor that properly cleans up simulation host.
     */
    ~RtlSimCommunicator();

    /**
     * Initialize the simulator and establish communication.
     * Must be called before using any communication methods.
     * With the directory constructor this spawns the simulator process and waits for its ack.
     */
    void initialize();

    /**
     * Shutdown the simulator and close communication.
     */
    void shutdown();

    /**
     * Read data from a tile core: any address and size, over the whole-word transfers the simulator
     * moves.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to read from
     * @param data Buffer to store read data
     * @param size Number of bytes to read
     */
    void tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size);

    /**
     * Write data to a tile core: any address and size. A partial first or last word is read, merged
     * and written back whole, so the bytes around the range keep their values.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to write to
     * @param data Data to write
     * @param size Number of bytes to write
     */
    void tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size);

    /**
     * Read whole words of device memory addressed by a flat global address, for a NOC that resolves
     * the destination from the address itself. No coordinate is sent. The simulator moves only whole
     * words: @p addr and @p size must be multiples of 4.
     *
     * @param addr Global address to read from
     * @param data Buffer to store read data
     * @param size Number of bytes to read
     */
    void global_read_words(uint64_t addr, void *data, uint32_t size);

    /**
     * Write whole words of device memory addressed by a flat global address. @see global_read_words().
     *
     * @param addr Global address to write to
     * @param data Data to write
     * @param size Number of bytes to write
     */
    void global_write_words(uint64_t addr, const void *data, uint32_t size);

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
    SimulationHost &get_host();

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
    // Which command pair an access uses and whether a core coordinate travels with it.
    enum class AccessKind { kTile, kGlobal, kSmn };

    // One whole-word read or write at a word-aligned address and size. Called with the channel's request_mutex() held.
    void read_words(AccessKind kind, tt_xy_pair core, uint64_t addr, void *data, uint32_t size);
    void write_words(AccessKind kind, tt_xy_pair core, uint64_t addr, const void *data, uint32_t size);

    // Any address and size over whole-word transfers (see word_access.hpp). Called with the channel's request_mutex()
    // held.
    void read_bytes(AccessKind kind, tt_xy_pair core, uint64_t addr, void *data, uint32_t size);
    void write_bytes(AccessKind kind, tt_xy_pair core, uint64_t addr, const void *data, uint32_t size);

    // Simulator directory path.
    std::filesystem::path simulator_directory_;

    // Single-socket session, when constructed from a simulator directory.
    std::unique_ptr<RtlSimSession> owned_session_;

    // The session socket this communicator talks over, when constructed from one; keeps the session alive.
    std::optional<RtlSimSocket> socket_;

    // The socket's channel: its receive thread, response queue, request and send locks, and host
    // memory callbacks. Shared by every communicator on the socket.
    std::shared_ptr<RtlSimChannel> channel_;

    // Whether initialize() attached this communicator to the channel and shutdown() hasn't detached it.
    bool attached_ = false;
};

}  // namespace tt::umd
