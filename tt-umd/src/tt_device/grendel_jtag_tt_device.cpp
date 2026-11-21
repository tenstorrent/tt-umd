// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/grendel_jtag_tt_device.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "grendel_flat_address.hpp"
#include "grendel_jtag_tt_device_impl.hpp"
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
    return GrendelJtagTTDeviceFactory::create(soc_descriptor, chiplet_number, [&](uint32_t tap) {
        return make_grendel_jtag_transport(host, port, tap, version);
    });
}

std::unique_ptr<GrendelJtagTTDevice> GrendelJtagTTDeviceFactory::create(
    const SocDescriptor& soc_descriptor, uint32_t chiplet_number, const GrendelJtagTransportFactory& open_tap) {
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

    std::vector<uint32_t> taps;
    for (const auto& channel : channels) {
        UMD_ASSERT(
            channel.tap.has_value() || channels.size() == 1,
            error::RuntimeError,
            fmt::format("GrendelJtagTTDevice needs a tap on Mimir {}: the package has more than one.", channel.chiplet));
        taps.push_back(channel.tap.value_or(chiplet_number));
    }
    const KeraunosChiplet& keraunos = soc_descriptor.get_arch_descriptor().get_keraunos();
    if (!keraunos.chiplet.empty()) {
        UMD_ASSERT(
            keraunos.tap.has_value(), error::RuntimeError, "Keraunos JTAG needs the tap field from the descriptor.");
        taps.push_back(*keraunos.tap);
    }
    // The descriptor rejects duplicate taps it lists. This catches a chiplet_number fallback on the Keraunos tap.
    UMD_ASSERT(
        std::unordered_set<uint32_t>(taps.begin(), taps.end()).size() == taps.size(),
        error::RuntimeError,
        fmt::format("GrendelJtagTTDevice opens two chiplets on one tap: {}.", fmt::join(taps, ", ")));

    std::vector<std::shared_ptr<chippy::transport::TransportInterface>> transports;
    transports.reserve(taps.size());
    for (const uint32_t tap : taps) {
        transports.push_back(open_tap(tap));
    }
    // Core accesses go through the flat-address map, which routes each chiplet to its own
    // transport. The protocol carries only the mmio id, so its provider never selects a chiplet.
    auto protocol = make_grendel_jtag_protocol(
        [first = transports.front()](tt_xy_pair) { return first; }, static_cast<int>(chiplet_number));
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
