// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/emu_tt_device.hpp"

#include <fmt/format.h>

#include <functional>

#include "emu_axi_transport.h"       // chippy
#include "jtag2axi_v2_transport.h"   // chippy
#include "mimir.h"              // chippy
#include "umd/device/coordinates/att/att_resolver.hpp"
#include "umd/device/coordinates/att/configs/mimir_1x1_att_map.hpp"
#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"


namespace tt::umd {

namespace {

// chippy word-granular ops use 32-bit accesses when given this minimum word size. The transport
// picks its batched series path from the transfer size and alignment alone, so this does not cost
// bulk throughput -- it only sets the granule an unaligned or partial access is decomposed into.
constexpr size_t kMinWordSizeBytes = 4;

// Mimir's address layout comes from chippy (lib/arch/grendel/mimir.h), which owns every address
// below the flat one.
//
// LOCAL, not SPA. chippy reaches an emu chiplet in local addressing -- GrendelAxiEmu constructs its
// chiplets with init_chiplets(metadata, /*use_spa_addressing_for_local_chiplet=*/false, false) --
// and the model agrees: test_sival_server.py wires the protocol straight to tb.axi.read_reg32, the
// chiplet's own AXI master, and the model's run_cce_via_sival.py addresses CCE SRAM at its local
// 0x40000000 while listing the SPA base 0x1280000000 as a separate constant. The package SPA bases
// (kMimir_0_SpaBaseAddr and friends) apply when a Mimir is reached through the fabric of a larger
// package, which is not this path.
const uint64_t kMimirConfigLocalBase = chippy::grendel::kMimirSmcLocalAddr;
const uint64_t kMimirConfigStride = chippy::grendel::kMimirConfigSize;
const uint64_t kMimirGddrDramLocalBase = chippy::grendel::kMimirGddrDramLocalAddr;

}  // namespace

const att::MapData& EmuTTDevice::mimir_att_map(const SocDescriptor& soc_descriptor) {
    // The map is static data (att::Table holds a pointer into it), so this validates the descriptor
    // against the map rather than deriving the map from the descriptor. A mismatch here means the
    // two have drifted apart, and a resolve would otherwise fail later with a bare "no endpoint".
    const std::vector<CoreCoord> smc_cores = soc_descriptor.get_cores(CoreType::SMC, CoordSystem::NOC0);
    UMD_ASSERT(
        smc_cores.size() == 1,
        error::RuntimeError,
        fmt::format("A Mimir descriptor must carry exactly one SMC core, found {}.", smc_cores.size()));

    const std::vector<CoreCoord> dram_cores = soc_descriptor.get_cores(CoreType::DRAM, CoordSystem::NOC0);
    const auto& dram_words = att::MIMIR_1X1_MAP.endpoint_words[static_cast<size_t>(att::WindowClass::Dram)];
    UMD_ASSERT(
        dram_cores.size() == dram_words.size(),
        error::RuntimeError,
        fmt::format(
            "A Mimir descriptor must carry {} DRAM cores to match the ATT map, found {}.",
            dram_words.size(),
            dram_cores.size()));

    // chippy names one DRAM base in the local view, so the slot width is the descriptor's own bank
    // size. Too small a slot would land channel 1 inside channel 0; DramCoresDoNotAlias is what
    // proves it against a real model.
    const uint64_t bank_size = soc_descriptor.get_arch_descriptor().get_dram_bank_size();
    const uint64_t slot = att::MIMIR_1X1_MAP.windows[static_cast<size_t>(att::WindowClass::Dram)].local_address_limit();
    UMD_ASSERT(
        bank_size == slot,
        error::RuntimeError,
        fmt::format("Descriptor dram_bank_size 0x{:x} does not match the ATT map DRAM slot 0x{:x}.", bank_size, slot));

    return att::MIMIR_1X1_MAP;
}

// Owns the chippy transport, held through TransportInterface so the device is transport-agnostic:
// tile_read_bytes/tile_write_bytes go through the base's read/write, which every chippy transport
// implements. Shared rather than unique because chippy's decorator transports
// (MultiChipletEmuAxiTransport for a multi-chiplet model, SmcRemapTransport for SMC register
// access) take a shared inner transport, so a multi-chiplet package will wrap this rather than
// replace it.
//
// `open` is the protocol handshake a transport needs before its first access. emu_axi sends INIT to
// the command server; jtag2axi has none, because OpenOCD owns the TAP and is already attached by
// the time this device is built.
struct EmuTTDevice::Impl {
    std::shared_ptr<chippy::transport::TransportInterface> transport;
    std::function<void()> open;

    static std::unique_ptr<Impl> emu_axi(const std::string& host, uint32_t port) {
        auto transport = std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(host, port);
        return std::unique_ptr<Impl>(new Impl{transport, [transport] { transport->initialize(); }});
    }

    static std::unique_ptr<Impl> jtag2axi(const std::string& host, uint32_t port, size_t chiplet) {
        auto transport = std::make_shared<chippy::transport::jtag2axi::v2::Jtag2AxiV2Transport>(
            host, static_cast<uint16_t>(port), chiplet);
        return std::unique_ptr<Impl>(new Impl{transport, [] {}});
    }
};

/* static */ std::unique_ptr<EmuTTDevice> EmuTTDevice::create(
    const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port) {
    return create(soc_descriptor, Transport::EmuAxi, host, port);
}

/* static */ std::unique_ptr<EmuTTDevice> EmuTTDevice::create(
    const SocDescriptor& soc_descriptor,
    Transport transport,
    const std::string& host,
    uint32_t port,
    size_t chiplet) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format("EmuTTDevice requires a GRENDEL descriptor, got {}.", arch_to_str(soc_descriptor.arch)));
    auto impl = transport == Transport::Jtag2Axi ? Impl::jtag2axi(host, port, chiplet)
                                                 : Impl::emu_axi(host, port);
    return std::unique_ptr<EmuTTDevice>(new EmuTTDevice(soc_descriptor, std::move(impl)));
}

EmuTTDevice::EmuTTDevice(const SocDescriptor& soc_descriptor, std::unique_ptr<Impl> impl) :
    SimulationTTDevice(std::make_unique<SimulationTTDeviceModel>(soc_descriptor.arch)), impl_(std::move(impl)) {
    set_soc_descriptor(soc_descriptor);

    // Grendel's NOC ATT resolves a flat 64-bit address into a destination (x, y) plus a local
    // address, so the coordinate has to be flattened into the address before the access is issued.
    // Installed here, in the base's protected slot, so host_read/host_write apply it while the
    // CoreCoord -- and so its CoreType, which selects the window -- is still intact.
    noc_address_resolver_ = std::make_unique<att::Resolver>(mimir_att_map(soc_descriptor));

    // Whatever handshake this transport needs before its first access.
    impl_->open();
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
