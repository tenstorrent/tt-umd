// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/emu_tt_device.hpp"

#include <fmt/format.h>

#include <cstring>
#include <vector>

#include "address_translation.h"  // chippy
#include "emu_axi_transport.h"    // chippy
#include "mimir.h"                // chippy
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
const uint64_t kMimirCceSramLocalBase = chippy::grendel::kMimirCce0SramLocalAddr;
const uint64_t kMimirCceSramStride = chippy::grendel::kMimirCceSramSize;

// Match Blackhole's DRISC convention: an address above the bank-relative GDDR range selects SRAM
// at the same DRAM coordinate. This is a UMD-side tag and is translated to Mimir's local 0x40000000
// CCE SRAM window before it reaches chippy.
constexpr uint64_t kMimirCceL1NocOffset = 0x2000000000ULL;

constexpr std::size_t kCcesPerMimir = 2;
}  // namespace

GrendelAddressWindows EmuTTDevice::mimir_address_windows(const SocDescriptor& soc_descriptor) {
    const std::vector<CoreCoord> smc_cores = soc_descriptor.get_cores(CoreType::SMC, CoordSystem::NOC0);
    UMD_ASSERT(
        smc_cores.size() == 1 || smc_cores.size() == 2,
        error::RuntimeError,
        fmt::format("A Mimir package descriptor must carry one or two SMC cores, found {}.", smc_cores.size()));
    UMD_ASSERT(
        soc_descriptor.get_num_dram_channels() == smc_cores.size(),
        error::RuntimeError,
        fmt::format(
            "A {}-Mimir package must expose {} DRAM channels (one per Mimir), found {}.",
            smc_cores.size(),
            smc_cores.size(),
            soc_descriptor.get_num_dram_channels()));

    GrendelAddressWindows windows{};
    windows.config_base = kMimirConfigLocalBase;
    windows.config_stride = kMimirConfigStride;
    windows.quasar_origin_x = smc_cores.front().x;
    windows.quasar_origin_y = smc_cores.front().y;
    windows.mesh_x_size = smc_cores.size();
    windows.mesh_y_size = 1;

    windows.dram_base = kMimirGddrDramLocalBase;
    // In local addressing chippy names one DRAM base, not a base per GDDR tile (only the SPA view
    // has Tile0/Tile1), so the channel spacing comes from the descriptor's own bank size. The
    // DramCoresDoNotAlias test is what proves this against a real model: too small a stride would
    // land channel 1 inside channel 0.
    windows.dram_stride = soc_descriptor.get_arch_descriptor().get_dram_bank_size();
    windows.dram_l1_noc_offset = kMimirCceL1NocOffset;
    windows.dram_l1_base = kMimirCceSramLocalBase;
    windows.dram_l1_stride = kMimirCceSramStride;
    windows.dram_l1_size = kMimirCceSramStride;

    const tt_xy_pair grid = soc_descriptor.get_grid_size(CoreType::SMC);
    windows.neo_x_start = std::max<uint32_t>(grid.x, 1) + 1;
    windows.neo_y_start = std::max<uint32_t>(grid.y, 1) + 1;
    windows.neo_x_count = 1;
    windows.neo_y_count = 1;

    return windows;
}

// Owns one socket and one local-address chippy view per Mimir. In MMK the server exposes each
// Mimir's identical local AXI map behind SELECT_CHIPLET, so the synthetic non-overlapping windows
// emitted by the resolver are translated back to a selected chiplet's local address here.
struct EmuTTDevice::Impl {
    using ChippyMemory = chippy::address_translation::Memory<std::uint32_t>;
    using Transport = chippy::transport::TransportInterface;

    struct MemoryRegion {
        uint64_t flat_base;
        uint64_t local_base;
        std::shared_ptr<Transport> transport;
        std::unique_ptr<ChippyMemory> memory;

        bool contains(uint64_t address, std::size_t size) const {
            if (address < flat_base) {
                return false;
            }
            const uint64_t region_offset = address - flat_base;
            return region_offset < memory->size_bytes() && size <= memory->size_bytes() - region_offset;
        }

        std::size_t offset(uint64_t address) const { return static_cast<std::size_t>(address - flat_base); }

        uint64_t local_address(uint64_t address) const { return local_base + offset(address); }

        void read(uint64_t address, void* dst, std::size_t size) const {
            const std::size_t byte_offset = offset(address);
            if (address % kMinWordSizeBytes == 0 && size % kMinWordSizeBytes == 0) {
                const auto bytes = memory->bulk_read_bytes(byte_offset, size);
                std::memcpy(dst, bytes.data(), bytes.size());
                return;
            }
            transport->read(size, kMinWordSizeBytes, local_address(address), dst);
        }

        void write(uint64_t address, const void* src, std::size_t size) {
            const std::size_t byte_offset = offset(address);
            if (address % kMinWordSizeBytes == 0 && size % kMinWordSizeBytes == 0) {
                std::vector<uint8_t> bytes(size);
                std::memcpy(bytes.data(), src, size);
                memory->bulk_write_bytes(byte_offset, bytes);
                return;
            }
            // chippy's write() takes a non-const void* even though it only reads the buffer.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            transport->write(size, kMinWordSizeBytes, local_address(address), const_cast<void*>(src));
        }
    };

    struct MimirContext {
        std::shared_ptr<Transport> transport;
        std::unique_ptr<chippy::grendel::Mimir> mimir;
    };

    std::shared_ptr<chippy::transport::emu_axi::EmuAxiTransport> root_transport;
    std::vector<MimirContext> mimirs;
    bool use_global_addressing = false;
    std::vector<MemoryRegion> memory_regions;

    Impl(const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port) :
        root_transport(std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(host, port)) {
        const auto windows = EmuTTDevice::mimir_address_windows(soc_descriptor);
        const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();

        mimirs.reserve(mimir_count);
        for (std::size_t mimir_index = 0; mimir_index < mimir_count; ++mimir_index) {
            std::shared_ptr<Transport> chiplet_transport = root_transport;
            if (mimir_count > 1) {
                chiplet_transport = std::make_shared<chippy::transport::emu_axi::MultiChipletEmuAxiTransport>(
                    root_transport, fmt::format("m{}", mimir_index));
            }
            mimirs.push_back(
                {.transport = chiplet_transport,
                 .mimir = std::make_unique<chippy::grendel::Mimir>(
                     chiplet_transport,
                     chippy::grendel::ChipletMetadata(chippy::grendel::ChipletType::Mimir, mimir_index, mimir_index),
                     /*use_spa_addressing=*/false)});

            const uint64_t flat_config_base = windows.config_base + mimir_index * windows.config_stride;
            memory_regions.push_back(
                make_region(flat_config_base, kMimirConfigLocalBase, windows.config_stride, chiplet_transport));
        }

        const uint32_t locations_per_channel =
            static_cast<uint32_t>(soc_descriptor.get_grid_size(CoreType::DRAM).y);
        UMD_ASSERT(
            locations_per_channel == kCcesPerMimir,
            error::RuntimeError,
            fmt::format(
                "Each Mimir DRAM channel must expose {} locations, found {}.",
                kCcesPerMimir,
                locations_per_channel));
        for (uint32_t channel = 0; channel < soc_descriptor.get_num_dram_channels(); ++channel) {
            const auto& chiplet_transport = mimirs.at(channel).transport;
            memory_regions.push_back(make_region(
                windows.dram_base + static_cast<uint64_t>(channel) * windows.dram_stride,
                kMimirGddrDramLocalBase,
                windows.dram_stride,
                chiplet_transport));
            for (uint32_t location = 0; location < locations_per_channel; ++location) {
                const uint32_t l1_index = channel * locations_per_channel + location;
                memory_regions.push_back(make_region(
                    windows.dram_l1_base + static_cast<uint64_t>(l1_index) * windows.dram_l1_stride,
                    kMimirCceSramLocalBase + location * kMimirCceSramStride,
                    windows.dram_l1_size,
                    chiplet_transport));
            }
        }
    }

    MemoryRegion make_region(
        uint64_t flat_base, uint64_t local_base, std::size_t size, const std::shared_ptr<Transport>& transport) {
        return {
            .flat_base = flat_base,
            .local_base = local_base,
            .transport = transport,
            .memory =
                std::make_unique<ChippyMemory>(transport.get(), local_base, local_base, use_global_addressing, size)};
    }

    MemoryRegion* find_memory(uint64_t address, std::size_t size) {
        for (auto& region : memory_regions) {
            if (region.contains(address, size)) {
                return &region;
            }
        }
        return nullptr;
    }
};

/* static */ std::unique_ptr<EmuTTDevice> EmuTTDevice::create(
    const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::QUASAR || soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format(
            "EmuTTDevice requires a QUASAR (or GRENDEL package) descriptor, got {}.",
            arch_to_str(soc_descriptor.arch)));
    return std::unique_ptr<EmuTTDevice>(
        new EmuTTDevice(soc_descriptor, std::make_unique<Impl>(soc_descriptor, host, port)));
}

EmuTTDevice::EmuTTDevice(const SocDescriptor& soc_descriptor, std::unique_ptr<Impl> impl) :
    SimulationTTDevice(std::make_unique<SimulationTTDeviceModel>(soc_descriptor.arch)), impl_(std::move(impl)) {
    set_soc_descriptor(soc_descriptor);

    // Grendel's NOC ATT resolves a flat 64-bit address into a destination (x, y) plus a local
    // address, so the coordinate has to be flattened into the address before the access is issued.
    // Installed here, in the base's protected slot, so host_read/host_write apply it while the
    // CoreCoord -- and so its CoreType, which selects the window -- is still intact.
    noc_address_resolver_ =
        std::make_unique<GrendelNocAddressResolver>(get_soc_descriptor(), mimir_address_windows(soc_descriptor));

    // INIT resets/initializes the model; send it once on the root socket, never once per chiplet.
    impl_->root_transport->initialize();
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
    if (auto* region = impl_->find_memory(addr, size)) {
        region->read(addr, mem_ptr, size);
        return;
    }
    UMD_THROW(
        error::RuntimeError,
        fmt::format("EmuTTDevice read at flat address 0x{:x} ({} bytes) is outside exposed Mimir memory.", addr, size));
}

void EmuTTDevice::tile_write_bytes(tt_xy_pair /*core*/, uint64_t addr, const void* mem_ptr, size_t size) {
    if (auto* region = impl_->find_memory(addr, size)) {
        region->write(addr, mem_ptr, size);
        return;
    }
    UMD_THROW(
        error::RuntimeError,
        fmt::format(
            "EmuTTDevice write at flat address 0x{:x} ({} bytes) is outside exposed Mimir memory.", addr, size));
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

void EmuTTDevice::apply_cce_pf_ctrl_reset(CoreCoord /*core*/, uint32_t cce_index, uint64_t hart_bits, bool release) {
    UMD_ASSERT(
        cce_index < impl_->mimirs.size() * kCcesPerMimir,
        error::RuntimeError,
        fmt::format("Mimir CCE index {} is out of range.", cce_index));

    auto& cce = impl_->mimirs.at(cce_index / kCcesPerMimir).mimir->cce(cce_index % kCcesPerMimir);
    auto reset = cce.registers.pf_ctrl.reset.read();
    reset.fields.uncore_reset = 1;
    // hart_bits is the PF_CTRL word (bit N+1 = hart N). core_reset is that field unshifted.
    const uint64_t hart_mask = hart_bits >> 1;
    if (release) {
        reset.fields.core_reset |= hart_mask;
    } else {
        reset.fields.core_reset &= ~hart_mask;
    }
    cce.registers.pf_ctrl.reset.write(reset);
}

}  // namespace tt::umd
