// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

#include "umd/device/coordinates/att/att_map.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/simulation_tt_device.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class TlbWindow;

/**
 * A Grendel package reached over chippy's emu_axi transport: a TCP command server that passes
 * reads and writes to the emulation test framework (grendelemulation's test_sival_server), or
 * chippy's register-map-backed mock_server.py, which speaks the same protocol and needs no
 * emulator.
 *
 * This is a simulation backend rather than a Grendel silicon device on purpose. The pre-silicon
 * paths already share everything that matters here -- the coordinate translation, the flat-address
 * resolution, and the tile access hooks all live in SimulationTTDevice -- so an emu backend only
 * has to say how bytes reach the target. A silicon TTDevice for Grendel is a separate concern and
 * will not inherit from this.
 *
 * chippy's headers require C++20 and must not leak into UMD's public API, so the transport lives
 * behind a pimpl and every chippy type stays inside the (C++20) translation unit.
 */
class EmuTTDevice : public SimulationTTDevice {
public:
    /** Which chippy transport carries this device's accesses. */
    enum class Transport {
        /** The emu_axi text protocol over TCP to the emulation command server, which drives the
            model's own AXI transactor. */
        EmuAxi,
        /** chippy's JTAG2AXI bridge, reached through an OpenOCD already attached to the model's
            jtag_vpi server. The emulated TAP carries the access instead of the AXI transactor. */
        Jtag2Axi,
    };

    /**
     * Connect to an emu_axi command server.
     *
     * @param soc_descriptor Full descriptor for the package (Grendel has no fixed floorplan, so it
     *                       always comes from YAML). Must describe ARCH::GRENDEL.
     * @param host Server host, as published by the orchestrator in silval_server_info.json.
     * @param port Server port.
     */
    static std::unique_ptr<EmuTTDevice> create(
        const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port);

    /**
     * Connect over a chosen transport.
     *
     * @param transport Which chippy transport to use.
     * @param host      Server host. For Jtag2Axi this is the OpenOCD RPC endpoint, not the
     *                  jtag_vpi server: chippy speaks to OpenOCD, which owns the TAP.
     * @param port      Server port. OpenOCD's RPC default is 6666.
     * @param chiplet   0-indexed chiplet on the JTAG chain. Ignored for EmuAxi, where the server
     *                  selects the chiplet instead.
     */
    static std::unique_ptr<EmuTTDevice> create(
        const SocDescriptor& soc_descriptor,
        Transport transport,
        const std::string& host,
        uint32_t port,
        size_t chiplet = 0);

    /**
     * As above, but with the ATT map named rather than inferred.
     *
     * A multi-die package presents one descriptor per die and they cannot be told apart by arch --
     * every Grendel die reports ARCH::GRENDEL -- so the caller, which knows which die it is opening,
     * names the map. Prefer this over the overload above wherever more than one die is in play.
     */
    static std::unique_ptr<EmuTTDevice> create(
        const SocDescriptor& soc_descriptor,
        const att::MapData& map,
        Transport transport,
        const std::string& host,
        uint32_t port,
        size_t chiplet = 0);

    ~EmuTTDevice() override;

    /**
     * The ATT map a single-Mimir package resolves through, in the local addressing the emu path
     * uses. Validated against @p soc_descriptor, which must name exactly the cores the map does.
     *
     * Exposed so the mapping has one definition: tests assert against it directly rather than
     * restating the bases, and a caller building its own resolver cannot drift from the device.
     */
    static const att::MapData& mimir_att_map(const SocDescriptor& soc_descriptor);

    /**
     * One die of a multi-die package: which core stands for it, where its JTAG probe sits, and
     * where its SMC bases.
     */
    struct DieBinding {
        /** The descriptor core that routes to this die, named as a caller would name it. */
        CoreCoord core;
        /** Chiplet index chippy's OpenOCD router demultiplexes on. */
        size_t chiplet;
    };

    /**
     * A multi-die package as one device, each die reached over its own JTAG probe.
     *
     * All the bindings share one endpoint: chippy's router reads the chiplet index out of each
     * command and forwards it to that die's OpenOCD, so several dies are several indices on one
     * socket rather than several connections.
     *
     * A binding is a chiplet index and nothing else, because over JTAG a die's address map does
     * not depend on which die it is: chippy reaches the SMC of every Mimir and Keraunos in an MMK
     * package at the same core-local address and changes only the chiplet the command is tagged
     * with. So there is no per-die base to carry, and no NocAddressResolver is installed -- the
     * address the caller passes is the address that goes on the wire.
     *
     * That is worth stating because the flat/local map tempts the opposite design. Keraunos does
     * base its SMC at 0x8000000 there, and addressing it that way over JTAG returns a bus DECERR:
     * the local view and the core-local alias this path uses are different views of the part, not
     * the same numbers.
     *
     * @param soc_descriptor Descriptor naming one core per die. Must describe ARCH::GRENDEL.
     * @param dies           One binding per die. Each core is translated through the descriptor
     *                       exactly as host_write translates it, so a binding and an access name
     *                       the same die whichever coordinate system the caller works in.
     * @param host,port      The router endpoint.
     */
    static std::unique_ptr<EmuTTDevice> create_multi_die(
        const SocDescriptor& soc_descriptor,
        const std::vector<DieBinding>& dies,
        Transport transport,
        const std::string& host,
        uint32_t port);

protected:
    SimulationBackendType backend_type() const override;

    void tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) override;
    void tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) override;

    // Grendel has no TLBs: the destination is encoded in the flat address by the resolver, so the
    // tile path is used directly and no window is ever allocated. Mirrors what the RTL backend
    // does for Quasar.
    bool should_use_cached_tlb_window() override;
    std::unique_ptr<TlbWindow> create_tlb_window(
        int tlb_index, size_t size, TlbMapping mapping, tlb_data config) override;

private:
    struct Impl;

    EmuTTDevice(const SocDescriptor& soc_descriptor, const att::MapData& map, std::unique_ptr<Impl> impl);

    // Package form: no ATT map, so no resolver. Addresses stay die-local and tile_*_bytes adds the
    // binding's base.
    EmuTTDevice(const SocDescriptor& soc_descriptor, std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::umd
