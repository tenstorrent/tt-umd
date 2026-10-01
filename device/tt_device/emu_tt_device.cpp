// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/emu_tt_device.hpp"

#include <fmt/format.h>

#include "emu_axi_transport.h"    // chippy
#include "mimir.h"                // chippy
#include "mimir_chippy_memory.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"
#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr std::size_t kCcesPerMimir = 2;

}  // namespace

GrendelAddressWindows EmuTTDevice::mimir_address_windows(const SocDescriptor& soc_descriptor) {
    return mimir_local_address_windows(soc_descriptor);
}

// Owns one socket and one local-address chippy view per Mimir. In MMK the server exposes each
// Mimir's identical local AXI map behind SELECT_CHIPLET, so the synthetic non-overlapping windows
// emitted by the resolver are translated back to a selected chiplet's local address by
// MimirChippyMemoryMap.
struct EmuTTDevice::Impl {
    using Transport = chippy::transport::TransportInterface;

    struct MimirContext {
        std::shared_ptr<Transport> transport;
        std::unique_ptr<chippy::grendel::Mimir> mimir;
    };

    std::shared_ptr<chippy::transport::emu_axi::EmuAxiTransport> root_transport;
    std::vector<MimirContext> mimirs;
    std::unique_ptr<MimirChippyMemoryMap> memory;

    Impl(const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port) :
        root_transport(std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(host, port)) {
        const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
        std::vector<std::shared_ptr<Transport>> chiplet_transports;
        chiplet_transports.reserve(mimir_count);
        mimirs.reserve(mimir_count);
        for (std::size_t mimir_index = 0; mimir_index < mimir_count; ++mimir_index) {
            std::shared_ptr<Transport> chiplet_transport = root_transport;
            if (mimir_count > 1) {
                chiplet_transport = std::make_shared<chippy::transport::emu_axi::MultiChipletEmuAxiTransport>(
                    root_transport, fmt::format("m{}", mimir_index));
            }
            chiplet_transports.push_back(chiplet_transport);
            mimirs.push_back(
                {.transport = chiplet_transport,
                 .mimir = std::make_unique<chippy::grendel::Mimir>(
                     chiplet_transport,
                     chippy::grendel::ChipletMetadata(chippy::grendel::ChipletType::Mimir, mimir_index, mimir_index),
                     /*use_spa_addressing=*/false)});
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
    // Installed here, in the base's protected slot, so host_read/host_write apply it while the
    // CoreCoord -- and so its CoreType, which selects the window -- is still intact.
    noc_address_resolver_ =
        std::make_unique<GrendelNocAddressResolver>(get_soc_descriptor(), mimir_address_windows(soc_descriptor));

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

// `core` arrives already translated and `addr` already flattened by the base, so the coordinate is
// deliberately unused: on Grendel the destination travels inside the address, not beside it.
void EmuTTDevice::tile_read_bytes(tt_xy_pair /*core*/, uint64_t addr, void* mem_ptr, size_t size) {
    impl_->memory->read(addr, mem_ptr, size);
}

void EmuTTDevice::tile_write_bytes(tt_xy_pair /*core*/, uint64_t addr, const void* mem_ptr, size_t size) {
    impl_->memory->write(addr, mem_ptr, size);
}

void EmuTTDevice::write_cce_reset_vector_register(
    CoreCoord /*core*/, uint32_t cce_index, uint32_t hart, uint64_t reset_vector) {
    UMD_ASSERT(
        cce_index < impl_->mimirs.size() * kCcesPerMimir,
        error::RuntimeError,
        fmt::format("Mimir CCE index {} is out of range.", cce_index));
    UMD_ASSERT(
        hart < chippy::grendel::registers::mimir::mimir_cce::TtClusterCtrlAddrMapAccessor{}.reset_vector.size(),
        error::RuntimeError,
        fmt::format("Mimir CCE hart index {} is out of range.", hart));

    chippy::grendel::registers::mimir::mimir_cce::ResetVectorRegAccessor value{};
    value.set_data(reset_vector);
    impl_->mimirs.at(cce_index / kCcesPerMimir)
        .mimir->cce(cce_index % kCcesPerMimir)
        .registers.tt_cluster_ctrl.reset_vector[hart]
        .write(value);
}

void EmuTTDevice::apply_cce_pf_ctrl_reset(
    CoreCoord /*core*/, uint32_t cce_index, uint64_t hart_bits, bool release, bool reset_uncore) {
    UMD_ASSERT(
        cce_index < impl_->mimirs.size() * kCcesPerMimir,
        error::RuntimeError,
        fmt::format("Mimir CCE index {} is out of range.", cce_index));

    auto& cce = impl_->mimirs.at(cce_index / kCcesPerMimir).mimir->cce(cce_index % kCcesPerMimir);
    auto reset = cce.registers.pf_ctrl.reset.read();
    // Same polarity as the hart bits: 1 = released. Partial resets leave the uncore bit alone.
    if (reset_uncore) {
        reset.fields.uncore_reset = release ? 1 : 0;
    }
    if (hart_bits != 0) {
        // hart_bits is the PF_CTRL word (bit N+1 = hart N). core_reset is that field unshifted.
        const uint64_t hart_mask = hart_bits >> 1;
        if (release) {
            reset.fields.core_reset |= hart_mask;
        } else {
            reset.fields.core_reset &= ~hart_mask;
        }
    }
    cce.registers.pf_ctrl.reset.write(reset);
}

void EmuTTDevice::write_cce_dmrisc_remap_entry(
    CoreCoord /*core*/,
    uint32_t cce_index,
    uint32_t entry,
    uint64_t region_start,
    uint64_t region_end,
    uint64_t local_base) {
    UMD_ASSERT(
        cce_index < impl_->mimirs.size() * kCcesPerMimir,
        error::RuntimeError,
        fmt::format("Mimir CCE index {} is out of range.", cce_index));

    auto& remap_entry = impl_->mimirs.at(cce_index / kCcesPerMimir)
                            .mimir->cce(cce_index % kCcesPerMimir)
                            .registers.dmrisc_addr_remap.dmrisc_remap_entries.at(entry);

    auto start = remap_entry.region_start.read();
    start.fields.start_addr = region_start >> grendel::CCE_DMRISC_REMAP_ADDR_SHIFT;
    remap_entry.region_start.write(start);
    auto end = remap_entry.region_end.read();
    end.fields.end_addr = region_end >> grendel::CCE_DMRISC_REMAP_ADDR_SHIFT;
    remap_entry.region_end.write(end);
    auto remap = remap_entry.region_remap_start.read();
    remap.fields.remap_start_addr = local_base >> grendel::CCE_DMRISC_REMAP_ADDR_SHIFT;
    remap_entry.region_remap_start.write(remap);
    auto attrs = remap_entry.region_attrs.read();
    attrs.fields.valid = 1;
    remap_entry.region_attrs.write(attrs);
}

}  // namespace tt::umd
