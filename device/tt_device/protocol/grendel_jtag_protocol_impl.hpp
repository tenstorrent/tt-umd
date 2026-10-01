/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

// INTERNAL, chippy-aware header (C++20). Do NOT include from any public
// (device/api/...) header — chippy's headers require C++20 and must stay
// confined behind the GrendelJtagProtocol pimpl.

#include <functional>
#include <map>
#include <memory>
#include <utility>

#include "transport_interface.h"  // chippy
#include "umd/device/tt_device/protocol/grendel_jtag_protocol.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Supplies the chippy transport used to reach a given core. In production this
 * closes over a chippy Grendel (Issue: connection factory); tests inject a
 * provider that returns a chippy::transport::MockTransportInterface.
 */
using SmnTransportProvider = std::function<std::shared_ptr<chippy::transport::TransportInterface>(tt_xy_pair)>;

std::shared_ptr<chippy::transport::TransportInterface> make_grendel_jtag_transport(
    const std::string& host,
    uint16_t port,
    uint32_t chiplet_number,
    GrendelJtagTransportVersion version);

struct GrendelJtagProtocol::Impl {
    SmnTransportProvider provider;
    int mmio_id;
    // One transport per core, created lazily and reused.
    std::map<tt_xy_pair, std::shared_ptr<chippy::transport::TransportInterface>> transport_cache;

    explicit Impl(SmnTransportProvider provider, int mmio_id) : provider(std::move(provider)), mmio_id(mmio_id) {}

    std::shared_ptr<chippy::transport::TransportInterface> transport_for(tt_xy_pair core) {
        auto it = transport_cache.find(core);
        if (it != transport_cache.end()) {
            return it->second;
        }
        auto transport = provider(core);
        transport_cache.emplace(core, transport);
        return transport;
    }
};

/**
 * Builds a GrendelJtagProtocol around a transport provider. Production and tests
 * share this seam so the device can reuse the same OpenOCD transport as the Memory map.
 */
struct GrendelJtagProtocolTestAccess {
    static std::unique_ptr<GrendelJtagProtocol> make(SmnTransportProvider provider, int mmio_id = 0) {
        return std::unique_ptr<GrendelJtagProtocol>(
            new GrendelJtagProtocol(std::make_unique<GrendelJtagProtocol::Impl>(std::move(provider), mmio_id)));
    }
};

inline std::unique_ptr<GrendelJtagProtocol> make_grendel_jtag_protocol(
    SmnTransportProvider provider, int mmio_id = 0) {
    return GrendelJtagProtocolTestAccess::make(std::move(provider), mmio_id);
}

}  // namespace tt::umd
