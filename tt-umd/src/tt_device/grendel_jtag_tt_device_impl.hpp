// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// INTERNAL, chippy-aware header (C++20). Do not include from public UMD headers.

#include <cstdint>
#include <functional>
#include <memory>

#include "transport_interface.h"  // chippy
#include "tt-umd/tt_device/grendel_jtag_tt_device.hpp"

namespace tt::umd {

// Opens the transport for one JTAG chain index.
using GrendelJtagTransportFactory = std::function<std::shared_ptr<chippy::transport::TransportInterface>(uint32_t tap)>;

struct GrendelJtagTTDeviceFactory {
    /**
     * Builds the device with one transport per chiplet, opened in the order the flat-address map
     * consumes them: each Mimir channel, then Keraunos.
     *
     * A Mimir's TAP is its mimir_channels tap. A descriptor with one Mimir and no tap uses
     * chiplet_number. More than one Mimir without taps, a Keraunos without a tap, or two chiplets
     * on one TAP throw. chiplet_number is also the device's mmio id.
     */
    static std::unique_ptr<GrendelJtagTTDevice> create(
        const SocDescriptor& soc_descriptor, uint32_t chiplet_number, const GrendelJtagTransportFactory& open_tap);
};

}  // namespace tt::umd
