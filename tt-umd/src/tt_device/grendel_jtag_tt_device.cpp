// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/grendel_jtag_tt_device.hpp"

#include <fmt/format.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "grendel_flat_address.hpp"
#include "protocol/grendel_jtag_protocol_impl.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<GrendelJtagTTDevice> GrendelJtagTTDevice::create(
    const SocDescriptor& soc_descriptor,
    const std::string& host,
    uint16_t port,
    uint32_t chiplet_number,
    GrendelJtagTransportVersion version) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::QUASAR || soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format(
            "GrendelJtagTTDevice requires a QUASAR (or GRENDEL package) descriptor, got {}.",
            arch_to_str(soc_descriptor.arch)));
    const auto& channels = soc_descriptor.get_arch_descriptor().get_mimir_channels();
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    UMD_ASSERT(
        channels.size() == mimir_count && !channels.empty(),
        error::RuntimeError,
        fmt::format(
            "GrendelJtagTTDevice expected one mimir_channels entry per Mimir, found {} entries for {} SMC cores.",
            channels.size(),
            mimir_count));
    // Each OpenOCD socket carries one chiplet's local AXI map.
    UMD_ASSERT(
        soc_descriptor.get_arch_descriptor().get_grendel_address_space() == GrendelAddressSpace::MimirLocal,
        error::RuntimeError,
        "GrendelJtagTTDevice talks to a chiplet-local AXI map. The descriptor must set address_space: mimir_local.");

    // One attached chiplet keeps the caller's TAP index. A second chiplet in the descriptor is
    // attached at the TAP number in its chiplet id, so m1 opens chiplet 1 rather than being refused.
    const KeraunosChiplet& keraunos = soc_descriptor.get_arch_descriptor().get_keraunos();
    std::vector<std::shared_ptr<chippy::transport::TransportInterface>> transports;
    transports.reserve(channels.size() + (keraunos.chiplet.empty() ? 0 : 1));
    uint32_t first_tap = chiplet_number;
    for (const auto& channel : channels) {
        const uint32_t tap =
            channels.size() == 1 ? chiplet_number : static_cast<uint32_t>(std::stoul(channel.chiplet.substr(1)));
        if (transports.empty()) {
            first_tap = tap;
        }
        transports.push_back(make_grendel_jtag_transport(host, port, tap, version));
    }
    if (!keraunos.chiplet.empty()) {
        // The TAP is the descriptor field. The digits of "k0" are not a chain index. This socket
        // is for the flat-address map. The protocol below still returns the first Mimir transport.
        UMD_ASSERT(
            keraunos.tap.has_value(), error::RuntimeError, "Keraunos JTAG needs the tap field from the descriptor.");
        transports.push_back(make_grendel_jtag_transport(host, port, *keraunos.tap, version));
    }
    auto protocol = make_grendel_jtag_protocol(
        [transports](tt_xy_pair) { return transports.front(); }, static_cast<int>(first_tap));
    auto address_transport =
        make_grendel_flat_address_transport(soc_descriptor, std::move(transports), "GrendelJtagTTDevice");
    return std::unique_ptr<GrendelJtagTTDevice>(
        new GrendelJtagTTDevice(soc_descriptor, std::move(protocol), std::move(address_transport)));
}

GrendelJtagTTDevice::GrendelJtagTTDevice(
    const SocDescriptor& soc_descriptor,
    std::unique_ptr<GrendelJtagProtocol> protocol,
    std::unique_ptr<FlatAddressTransport> address_transport) :
    GrendelTTDevice(std::move(protocol), soc_descriptor, std::move(address_transport)) {}

GrendelJtagTTDevice::~GrendelJtagTTDevice() = default;

}  // namespace tt::umd
