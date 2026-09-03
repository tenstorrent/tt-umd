// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/emu_tt_device.hpp"

#include <fmt/format.h>

#include "emu_axi_transport.h"  // chippy
#include "mimir.h"              // chippy
#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"
#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"


namespace tt::umd {

namespace {

// chippy word-granular ops use 32-bit accesses when given this minimum word size. The transport
// picks its batched series path from the transfer size and alignment alone, so this does not cost
// bulk throughput -- it only sets the granule an unaligned or partial access is decomposed into.
constexpr size_t kMinWordSizeBytes = 4;

// Mimir's SPA layout comes from chippy (lib/arch/grendel/mimir.h), which owns every address below
// the flat one: taking the bases from there rather than restating them keeps one source of truth.
// GrendelAddressWindows' defaults describe a Quasar mesh -- a per-tile config aperture indexed
// row-major over a 10x6 mesh -- which does not describe a lone Mimir, whose config region has one
// slot per chiplet instance with the SMC's registers at offset 0 within it.
const uint64_t kMimirConfigSpaBase = chippy::grendel::get_config_spa_baseaddr_for_mimir(0);
const uint64_t kMimirConfigStride =
    chippy::grendel::get_config_spa_baseaddr_for_mimir(1) - chippy::grendel::get_config_spa_baseaddr_for_mimir(0);
const uint64_t kMimirGddrDramSpaBase = chippy::grendel::kMimirGddrDramTile0SpaBaseAddr;
// Derived, not assumed: the GDDR tiles are spaced by this much in SPA, which is NOT the 8 GiB bank
// size the descriptor reports. Indexing DRAM by bank size (as the Quasar defaults do) would alias
// channel 1 into channel 0's window.
const uint64_t kMimirGddrDramStride =
    chippy::grendel::kMimirGddrDramTile1SpaBaseAddr - chippy::grendel::kMimirGddrDramTile0SpaBaseAddr;

// A windows table for a single-Mimir package. The config window is indexed by chiplet instance
// rather than by mesh position, so the mesh is collapsed to 1x1 anchored on the SMC core: the one
// SMC resolves to instance 0. The NEO grid is parked outside the descriptor's grid because Mimir
// carries no compute and nothing may land in an L1 window -- validate() rejects a zero extent, so
// it cannot simply be emptied.
GrendelAddressWindows mimir_windows(const SocDescriptor& soc_descriptor) {
    const std::vector<CoreCoord> smc_cores = soc_descriptor.get_cores(CoreType::SMC, CoordSystem::NOC0);
    UMD_ASSERT(
        smc_cores.size() == 1,
        error::RuntimeError,
        fmt::format("A Mimir descriptor must carry exactly one SMC core, found {}.", smc_cores.size()));

    GrendelAddressWindows windows{};
    windows.config_base = kMimirConfigSpaBase;
    windows.config_stride = kMimirConfigStride;
    windows.quasar_origin_x = smc_cores.front().x;
    windows.quasar_origin_y = smc_cores.front().y;
    windows.mesh_x_size = 1;
    windows.mesh_y_size = 1;

    windows.dram_base = kMimirGddrDramSpaBase;
    windows.dram_stride = kMimirGddrDramStride;

    const tt_xy_pair grid = soc_descriptor.get_grid_size(CoreType::SMC);
    windows.neo_x_start = std::max<uint32_t>(grid.x, 1) + 1;
    windows.neo_y_start = std::max<uint32_t>(grid.y, 1) + 1;
    windows.neo_x_count = 1;
    windows.neo_y_count = 1;

    return windows;
}

}  // namespace

// Owns the chippy transport. Held by shared_ptr because chippy's decorator transports
// (MultiChipletEmuAxiTransport for a multi-chiplet model, SmcRemapTransport for SMC register
// access) take a shared inner transport, so a multi-chiplet package will wrap this rather than
// replace it.
struct EmuTTDevice::Impl {
    std::shared_ptr<chippy::transport::emu_axi::EmuAxiTransport> transport;

    Impl(const std::string& host, uint32_t port) :
        transport(std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(host, port)) {}
};

/* static */ std::unique_ptr<EmuTTDevice> EmuTTDevice::create(
    const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format("EmuTTDevice requires a GRENDEL descriptor, got {}.", arch_to_str(soc_descriptor.arch)));
    return std::unique_ptr<EmuTTDevice>(
        new EmuTTDevice(soc_descriptor, std::make_unique<Impl>(host, port)));
}

EmuTTDevice::EmuTTDevice(const SocDescriptor& soc_descriptor, std::unique_ptr<Impl> impl) :
    SimulationTTDevice(std::make_unique<SimulationTTDeviceModel>(soc_descriptor.arch)), impl_(std::move(impl)) {
    set_soc_descriptor(soc_descriptor);

    // Grendel's NOC ATT resolves a flat 64-bit address into a destination (x, y) plus a local
    // address, so the coordinate has to be flattened into the address before the access is issued.
    // Installed here, in the base's protected slot, so host_read/host_write apply it while the
    // CoreCoord -- and so its CoreType, which selects the window -- is still intact.
    noc_address_resolver_ = std::make_unique<GrendelNocAddressResolver>(get_soc_descriptor(), mimir_windows(soc_descriptor));

    // INIT opens the session with the command server.
    impl_->transport->initialize();
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

// `core` arrives already translated and `addr` already flattened by the base, so the coordinate is
// deliberately unused: on Grendel the destination travels inside the address, not beside it.
void EmuTTDevice::tile_read_bytes(tt_xy_pair /*core*/, uint64_t addr, void* mem_ptr, size_t size) {
    impl_->transport->read(size, kMinWordSizeBytes, addr, mem_ptr);
}

void EmuTTDevice::tile_write_bytes(tt_xy_pair /*core*/, uint64_t addr, const void* mem_ptr, size_t size) {
    // chippy's write() takes a non-const void* even though it only reads the buffer.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    impl_->transport->write(size, kMinWordSizeBytes, addr, const_cast<void*>(mem_ptr));
}

}  // namespace tt::umd
