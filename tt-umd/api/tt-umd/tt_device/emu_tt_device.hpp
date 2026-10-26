// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "tt-umd/coordinates/grendel_noc_address_resolver.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/grendel_tt_device.hpp"

namespace tt::umd {

class DeviceProtocol;

/**
 * emu_axi protocol for a GrendelTTDevice.
 *
 * Connects to chippy's TCP command server (a SiVal test server, or mock_server.py). The sockets
 * are chiplet-local, so the descriptor must set address_space: mimir_local. Reads and writes live
 * on GrendelTTDevice. The device model serves an emu protocol over those same sockets, so a
 * TTDevice call that still goes through DeviceProtocol reaches the emulator. chippy stays
 * inside the translation unit.
 */
class EmuTTDevice : public GrendelTTDevice {
public:
    /**
     * Connect to an emu_axi command server.
     *
     * @param soc_descriptor Full descriptor for the package, always from YAML. Architecture is
     *                       QUASAR; GRENDEL is accepted as the UMD package alias.
     * @param host Server host, as published by the orchestrator in silval_server_info.json.
     * @param port Server port.
     * @param send_init Send INIT on connect. INIT is a full model reset on the SiVal servers, so it
     *                  must be suppressed when bring-up was already performed by another client.
     */
    static std::unique_ptr<EmuTTDevice> create(
        const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port, bool send_init = true);

    ~EmuTTDevice() override;

    /**
     * Synthetic non-overlapping address windows for a one- or two-Mimir package.
     *
     * The Mimir-local transport maps each window back to the selected Mimir's local AXI address.
     * Exposed so tests assert against the same mapping rather than restating its bases.
     */
    static GrendelAddressWindows mimir_address_windows(const SocDescriptor& soc_descriptor);

private:
    EmuTTDevice(
        const SocDescriptor& soc_descriptor,
        std::unique_ptr<DeviceProtocol> protocol,
        std::unique_ptr<FlatAddressTransport> address_transport);
};

}  // namespace tt::umd
