// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/emu_tt_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "emu_axi_transport.h"  // chippy
#include "grendel_flat_address.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device_model/simulation_tt_device_model.hpp"

namespace tt::umd {

namespace {

// A multi-chiplet server answers GET_CHIPLET with a chiplet id ("k0", "m0"). The single-chiplet
// SiVal server answers "GET_CHIPLET called in single-chiplet model." and logs every SELECT_CHIPLET
// it ignores. Only an id means there is a chiplet to select. A failed probe keeps selecting: the
// MMK default chiplet is k0, and skipping the select there would program Keraunos.
bool looks_like_chiplet_id(std::string chiplet) {
    const auto begin = chiplet.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return false;
    }
    const auto end = chiplet.find_last_not_of(" \t");
    chiplet = chiplet.substr(begin, end - begin + 1);
    if (chiplet.size() < 2 || chiplet.size() > 3) {
        return false;
    }
    const char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(chiplet.front())));
    if (kind != 'k' && kind != 'm' && kind != 'q') {
        return false;
    }
    return std::all_of(chiplet.begin() + 1, chiplet.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool emu_server_selects_chiplets(const std::shared_ptr<chippy::transport::emu_axi::EmuAxiTransport>& root) {
    try {
        return looks_like_chiplet_id(root->get_chiplet());
    } catch (const std::exception&) {
        return true;
    }
}

}  // namespace

GrendelAddressWindows EmuTTDevice::mimir_address_windows(const SocDescriptor& soc_descriptor) {
    return mimir_local_address_windows(soc_descriptor);
}

/* static */ std::unique_ptr<EmuTTDevice> EmuTTDevice::create(
    const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port, bool send_init) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::QUASAR || soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format(
            "EmuTTDevice requires a QUASAR (or GRENDEL package) descriptor, got {}.",
            arch_to_str(soc_descriptor.arch)));
    // These sockets carry a chiplet-local AXI map. A package ATT address would miss every window.
    UMD_ASSERT(
        soc_descriptor.get_arch_descriptor().get_grendel_address_space() == GrendelAddressSpace::MimirLocal,
        error::RuntimeError,
        "EmuTTDevice talks to a chiplet-local AXI map. The descriptor must set address_space: mimir_local.");

    auto root_transport = std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(host, port);
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    const bool select_chiplet = emu_server_selects_chiplets(root_transport);
    std::vector<std::shared_ptr<chippy::transport::TransportInterface>> chiplet_transports;
    chiplet_transports.reserve(mimir_count);
    for (std::size_t mimir_index = 0; mimir_index < mimir_count; ++mimir_index) {
        // A one-Mimir descriptor still selects on MMK: the server's default chiplet is k0, and
        // a naked socket would program Keraunos. A single-chiplet server has nothing to select,
        // and each SELECT_CHIPLET there is logged and ignored.
        std::shared_ptr<chippy::transport::TransportInterface> chiplet_transport = root_transport;
        if (select_chiplet) {
            chiplet_transport = std::make_shared<chippy::transport::emu_axi::MultiChipletEmuAxiTransport>(
                root_transport, fmt::format("m{}", mimir_index));
        }
        chiplet_transports.push_back(std::move(chiplet_transport));
    }
    auto address_transport =
        make_grendel_flat_address_transport(soc_descriptor, std::move(chiplet_transports), "EmuTTDevice");
    auto device = std::unique_ptr<EmuTTDevice>(new EmuTTDevice(soc_descriptor, std::move(address_transport)));

    // INIT resets/initializes the model; send it once on the root socket, never once per chiplet.
    // On the SiVal servers INIT re-runs the reset phase, which drops any bring-up an earlier client
    // performed, so it is suppressed when the caller attaches to an already-brought-up model.
    if (send_init) {
        root_transport->initialize();
    }
    return device;
}

EmuTTDevice::EmuTTDevice(
    const SocDescriptor& soc_descriptor, std::unique_ptr<FlatAddressTransport> address_transport) :
    GrendelTTDevice(
        std::make_unique<SimulationTTDeviceModel>(soc_descriptor), soc_descriptor, std::move(address_transport)) {}

// Deliberately does NOT tear the transport down. chippy's teardown() sends QUIT, and QUIT ends the
// session for whoever owns the server: the mock server sets its shutdown event, and on a real run
// it ends the emulation job. Ownership sits with the orchestrator, which sends QUIT itself once the
// test process has exited (run_validation_test.py's _send_quit_command) -- so a device that sent it
// would kill the server under any later device, or under a second test in the same process.
//
// chippy's transport exposes no close-without-QUIT, and ~EmuAxiTransport is defaulted, so the
// socket is released when the process exits rather than here. Worth a small chippy addition.
EmuTTDevice::~EmuTTDevice() = default;

}  // namespace tt::umd
