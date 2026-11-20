// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>

#include "tt-umd/simulation/rtl_sim_communicator.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

class RtlSimSocket;

/**
 * @brief Transport for one device of an RTL simulation or emulation build, over one access point.
 *
 * A plain transport: it sends what it is given and folds no coordinate into an address. Behind an ATT
 * (flat_addresses) an access is a global read or write of @p addr and its coordinate is ignored; otherwise
 * the coordinate travels with the core-local address. NocId::SYSTEM_NOC reaches the tile over the system
 * NOC (SMN), by coordinate either way. Every access is synchronous and ordered, so the ctrl variants are
 * the data ones, one register wide; no multicast is modelled.
 */
class RtlSimProtocol : public DeviceProtocol {
public:
    /** Over a session of its own on the build at @p simulator_directory, with one unnamed socket. */
    RtlSimProtocol(const std::filesystem::path& simulator_directory, bool flat_addresses, int mmio_id);

    /** Over @p socket, shared with the other devices talking over it. Keeps the session alive. */
    RtlSimProtocol(const RtlSimSocket& socket, bool flat_addresses, int mmio_id);

    /** Connect to the simulator; with a session of its own, launch it first. Before any access. */
    void initialize();

    /** Disconnect; the last device on the socket tells the simulator to exit. */
    void shutdown();

    /** Serve the simulator's reads and writes of host memory; set before initialize(). */
    void set_ram_callbacks(RtlSimCommunicator::RamWriteCallback write_cb, RtlSimCommunicator::RamReadCallback read_cb);

    // --- DeviceProtocol ---
    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(
        void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    void write_ctrl(
        const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;

    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;

    int get_mmio_id() override;

    /**
     * Access @p core by coordinate even behind an ATT: for programming the ATT itself, before it routes
     * anything, and for the low AXI user bits (@p user_bits, bit 8 is cce_cmd_snoop), which only a
     * coordinate access carries.
     */
    void tile_read_bytes(tt_xy_pair core, uint64_t addr, void* dst, size_t size, uint64_t user_bits = 0);
    void tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* src, size_t size, uint64_t user_bits = 0);

    // The testbench's reset commands, which the simulator takes in place of a reset register write.
    void all_tensix_reset_assert(tt_xy_pair core);
    void all_tensix_reset_deassert(tt_xy_pair core);
    void all_neo_dms_reset_assert(tt_xy_pair core);
    void all_neo_dms_reset_deassert(tt_xy_pair core);
    void neo_dm_reset_assert(tt_xy_pair core, uint32_t dm_index);
    void neo_dm_reset_deassert(tt_xy_pair core, uint32_t dm_index);
    void all_neo_dms_uncore_reset_assert();
    void all_neo_dms_uncore_reset_deassert();
    void neo_dm_uncore_reset_assert(tt_xy_pair core);
    void neo_dm_uncore_reset_deassert(tt_xy_pair core);

private:
    std::unique_ptr<RtlSimCommunicator> communicator_;
    bool flat_addresses_;
    int mmio_id_;
    // Makes each access atomic: the simulator moves whole words, so a partial one is read, merged and
    // written back with separate requests.
    std::mutex io_lock_;
};

}  // namespace tt::umd
