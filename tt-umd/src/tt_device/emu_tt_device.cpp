// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/emu_tt_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <vector>

#include "emu_axi_transport.h"  // chippy
#include "mimir_chippy_memory.hpp"
#include "tt-umd/coordinates/grendel_noc_address_resolver.hpp"
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

// Owns one socket per Mimir. In MMK the server exposes each Mimir's identical local AXI map
// behind SELECT_CHIPLET, so the synthetic non-overlapping windows emitted by the resolver are
// translated back to a selected chiplet's local address by MimirChippyMemoryMap.
struct EmuTTDevice::Impl {
    using Transport = chippy::transport::TransportInterface;

    std::shared_ptr<chippy::transport::emu_axi::EmuAxiTransport> root_transport;
    // Mimir's windows are not one of the package ATT maps the RTL backend selects with
    // TT_UMD_NOC_ATT, so this device flattens addresses itself instead of through the base resolver.
    std::unique_ptr<GrendelNocAddressResolver> address_resolver;
    std::unique_ptr<MimirChippyMemoryMap> memory;

    Impl(const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port) :
        root_transport(std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(host, port)) {
        const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
        const bool select_chiplet = emu_server_selects_chiplets(root_transport);
        std::vector<std::shared_ptr<Transport>> chiplet_transports;
        chiplet_transports.reserve(mimir_count);
        for (std::size_t mimir_index = 0; mimir_index < mimir_count; ++mimir_index) {
            // A one-Mimir descriptor still selects on MMK: the server's default chiplet is k0, and
            // a naked socket would program Keraunos. A single-chiplet server has nothing to select,
            // and each SELECT_CHIPLET there is logged and ignored.
            std::shared_ptr<Transport> chiplet_transport = root_transport;
            if (select_chiplet) {
                chiplet_transport = std::make_shared<chippy::transport::emu_axi::MultiChipletEmuAxiTransport>(
                    root_transport, fmt::format("m{}", mimir_index));
            }
            chiplet_transports.push_back(std::move(chiplet_transport));
        }
        memory = std::make_unique<MimirChippyMemoryMap>(
            soc_descriptor, std::move(chiplet_transports), "EmuTTDevice");
    }
};

/* static */ std::unique_ptr<EmuTTDevice> EmuTTDevice::create(
    const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port, bool send_init) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::QUASAR || soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format(
            "EmuTTDevice requires a QUASAR (or GRENDEL package) descriptor, got {}.",
            arch_to_str(soc_descriptor.arch)));
    return std::unique_ptr<EmuTTDevice>(
        new EmuTTDevice(soc_descriptor, std::make_unique<Impl>(soc_descriptor, host, port), send_init));
}

EmuTTDevice::EmuTTDevice(const SocDescriptor& soc_descriptor, std::unique_ptr<Impl> impl, bool send_init) :
    SimulationTTDevice(std::make_unique<SimulationTTDeviceModel>(soc_descriptor)), impl_(std::move(impl)) {
    set_soc_descriptor(soc_descriptor);

    // Grendel's NOC ATT resolves a flat 64-bit address into a destination (x, y) plus a local
    // address, so the coordinate has to be flattened into the address before the access is issued.
    // The base resolver is the package ATT map selected by TT_UMD_NOC_ATT; a lone Mimir uses its
    // own windows. Keep the resolver on this device (the base slot is an EndpointResolver) and
    // publish it so the shared CCE control plane can flatten register offsets.
    impl_->address_resolver =
        std::make_unique<GrendelNocAddressResolver>(get_soc_descriptor(), mimir_address_windows(soc_descriptor));
    set_grendel_address_resolver(impl_->address_resolver.get());

    // INIT resets/initializes the model; send it once on the root socket, never once per chiplet.
    // On the SiVal servers INIT re-runs the reset phase, which drops any bring-up an earlier client
    // performed, so it is suppressed when the caller attaches to an already-brought-up model.
    if (send_init) {
        impl_->root_transport->initialize();
    }
}

// Deliberately does NOT tear the transport down. chippy's teardown() sends QUIT, and QUIT ends the
// session for whoever owns the server: the mock server sets its shutdown event, and on a real run
// it ends the emulation job. Ownership sits with the orchestrator, which sends QUIT itself once the
// test process has exited (run_validation_test.py's _send_quit_command) -- so a device that sent it
// would kill the server under any later device, or under a second test in the same process.
//
// chippy's transport exposes no close-without-QUIT, and ~EmuAxiTransport is defaulted, so the
// socket is released when the process exits rather than here. Worth a small chippy addition.
EmuTTDevice::~EmuTTDevice() = default;

SimulationBackendType EmuTTDevice::backend_type() const { return SimulationBackendType::EMU_AXI; }

bool EmuTTDevice::should_use_cached_tlb_window() { return false; }

std::unique_ptr<TlbWindow> EmuTTDevice::create_tlb_window(
    int /*tlb_index*/, size_t /*size*/, TlbMapping /*mapping*/, tlb_data /*config*/) {
    // Unreachable while should_use_cached_tlb_window() is false, but the base declares it pure
    // virtual for the backends that do allocate windows.
    UMD_THROW(error::RuntimeError, "Grendel addresses cores by flat address and has no TLB windows.");
}

// `core` arrives already translated and `addr` is still core-local. Fold it into Mimir's windows,
// then hand the flat address to the chippy memory map.
uint64_t EmuTTDevice::flat_address(tt_xy_pair core, uint64_t addr) const {
    const CoreCoord core_coord = get_soc_descriptor().get_coord_at(core, CoordSystem::TRANSLATED);
    return impl_->address_resolver->to_flat_address(core_coord, addr, get_selected_noc_id());
}

void EmuTTDevice::tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) {
    impl_->memory->read(flat_address(core, addr), mem_ptr, size);
}

void EmuTTDevice::tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) {
    impl_->memory->write(flat_address(core, addr), mem_ptr, size);
}

void EmuTTDevice::read_flat_address(uint64_t addr, void* dst, size_t size) { impl_->memory->read(addr, dst, size); }

void EmuTTDevice::write_flat_address(uint64_t addr, const void* src, size_t size) {
    impl_->memory->write(addr, src, size);
}

}  // namespace tt::umd
