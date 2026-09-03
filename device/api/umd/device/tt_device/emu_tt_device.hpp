// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

    ~EmuTTDevice() override;

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

    EmuTTDevice(const SocDescriptor& soc_descriptor, std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::umd
