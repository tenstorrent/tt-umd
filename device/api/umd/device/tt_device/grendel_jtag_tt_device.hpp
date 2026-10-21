// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/grendel_tt_device.hpp"
#include "umd/device/tt_device/protocol/grendel_jtag_protocol.hpp"

namespace tt::umd {

class FlatAddressTransport;

/**
 * JTAG2AXI protocol for a GrendelTTDevice.
 *
 * The OpenOCD process and fabric bring-up are owned by the caller. This device only attaches to
 * the TCL RPC endpoint. The socket is chiplet-local, so the descriptor must set address_space:
 * mimir_local. Reads and writes live on GrendelTTDevice.
 */
class GrendelJtagTTDevice : public GrendelTTDevice {
public:
    static std::unique_ptr<GrendelJtagTTDevice> create(
        const SocDescriptor& soc_descriptor,
        const std::string& host,
        uint16_t port = kDefaultOpenOcdTclPort,
        uint32_t chiplet_number = 0,
        GrendelJtagTransportVersion version = GrendelJtagTransportVersion::V2);

    ~GrendelJtagTTDevice() override;

private:
    GrendelJtagTTDevice(
        const SocDescriptor& soc_descriptor,
        std::unique_ptr<GrendelJtagProtocol> protocol,
        std::unique_ptr<FlatAddressTransport> address_transport);
};

}  // namespace tt::umd
