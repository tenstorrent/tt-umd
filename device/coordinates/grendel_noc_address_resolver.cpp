// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"

#include <fmt/format.h>

#include <tt-logger/tt-logger.hpp>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

bool is_power_of_two(uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }

void check_stride(uint64_t stride, const char* name) {
    UMD_ASSERT(
        is_power_of_two(stride),
        error::RuntimeError,
        fmt::format(
            "Grendel address window '{}' stride 0x{:x} is not a power of two. The ATT recovers the "
            "window index by slicing addr[mask-1:ep_idx], so the stride must be 1 << ep_idx.",
            name,
            stride));
}

void check_offset(uint64_t offset, uint64_t stride, const char* name) {
    UMD_ASSERT(
        offset < stride,
        error::RuntimeError,
        fmt::format(
            "Offset 0x{:x} does not fit the Grendel '{}' window granule of 0x{:x} bytes; it would "
            "carry into the next window slot and target a different core.",
            offset,
            name,
            stride));
}

}  // namespace

bool GrendelAddressWindows::is_in_neo_grid(uint32_t noc0_x, uint32_t noc0_y) const {
    return noc0_x >= neo_x_start && noc0_x < neo_x_start + neo_x_count && noc0_y >= neo_y_start &&
           noc0_y < neo_y_start + neo_y_count;
}

uint64_t GrendelAddressWindows::tensix_l1_address(uint32_t noc0_x, uint32_t noc0_y, uint64_t offset) const {
    UMD_ASSERT(
        is_in_neo_grid(noc0_x, noc0_y),
        error::RuntimeError,
        fmt::format(
            "Core ({}, {}) is outside the NEO grid x=[{}, {}] y=[{}, {}] and has no L1 window.",
            noc0_x,
            noc0_y,
            neo_x_start,
            neo_x_start + neo_x_count - 1,
            neo_y_start,
            neo_y_start + neo_y_count - 1));
    check_offset(offset, neo_l1_stride, "TensixNEO L1");
    const uint64_t index =
        static_cast<uint64_t>(noc0_y - neo_y_start) * neo_x_count + static_cast<uint64_t>(noc0_x - neo_x_start);
    return neo_l1_base + index * neo_l1_stride + offset;
}

bool GrendelAddressWindows::is_in_mesh(uint32_t noc0_x, uint32_t noc0_y) const {
    return noc0_x >= quasar_origin_x && noc0_x < quasar_origin_x + mesh_x_size && noc0_y >= quasar_origin_y &&
           noc0_y < quasar_origin_y + mesh_y_size;
}

uint64_t GrendelAddressWindows::config_address(uint32_t noc0_x, uint32_t noc0_y, uint64_t offset) const {
    // Cores outside the Quasar mesh have no slot in this window's expansion. That includes the
    // Keraunos column (package x=11) and the Mimir D2D ingress rows (package y=0 and y=7), which
    // the soc descriptor also lists as cores -- they are reached through their own windows, so
    // falling through to a config index here would alias onto an unrelated Quasar tile.
    UMD_ASSERT(
        is_in_mesh(noc0_x, noc0_y),
        error::RuntimeError,
        fmt::format(
            "Core ({}, {}) is outside the Quasar mesh x=[{}, {}] y=[{}, {}] and has no config "
            "aperture in this window.",
            noc0_x,
            noc0_y,
            quasar_origin_x,
            quasar_origin_x + mesh_x_size - 1,
            quasar_origin_y,
            quasar_origin_y + mesh_y_size - 1));
    check_offset(offset, config_stride, "per-tile config");
    const uint64_t tile_id = static_cast<uint64_t>(noc0_y - quasar_origin_y) * mesh_x_size + (noc0_x - quasar_origin_x);
    return config_base + tile_id * config_stride + offset;
}

uint64_t GrendelAddressWindows::dram_address(uint32_t channel, uint64_t offset) const {
    check_offset(offset, dram_stride, "Mimir GDDR");
    return dram_base + static_cast<uint64_t>(channel) * dram_stride + offset;
}

void GrendelAddressWindows::validate() const {
    check_stride(neo_l1_stride, "TensixNEO L1");
    check_stride(config_stride, "per-tile config");
    check_stride(dram_stride, "Mimir GDDR");
    UMD_ASSERT(
        mesh_x_size > 0 && mesh_y_size > 0,
        error::RuntimeError,
        "Grendel address windows: the Quasar mesh extent must be non-zero.");
    UMD_ASSERT(
        neo_x_count > 0 && neo_y_count > 0,
        error::RuntimeError,
        "Grendel address windows: the NEO grid extent must be non-zero.");
}

GrendelNocAddressResolver::GrendelNocAddressResolver(
    const SocDescriptor& soc_descriptor, const GrendelAddressWindows& windows) :
    soc_descriptor_(&soc_descriptor), windows_(windows) {
    windows_.validate();
}

uint64_t GrendelNocAddressResolver::to_flat_address(const CoreCoord& core, uint64_t offset, NocId noc_id) const {
    // SMN has its own ATT instance and its own window set (including the bit-52 global prefix), so
    // it is not resolved here. Today SYSTEM_NOC accesses are dispatched by the backend's special
    // read/write hook before reaching this resolver, so this is a safety net rather than a path.
    UMD_ASSERT(
        noc_id != NocId::SYSTEM_NOC,
        error::RuntimeError,
        "System NOC (SMN) accesses are not resolved through the NOC ATT global windows.");

    // A LITERAL coordinate is already device-ready by contract (the socket path forwards
    // client-translated coordinates verbatim), so take it as NOC0 without re-translating.
    const tt_xy_pair noc0_xy = (core.coord_system == CoordSystem::LITERAL)
                                   ? tt_xy_pair(core.x, core.y)
                                   : tt_xy_pair(soc_descriptor_->translate_coord_to(core, CoordSystem::NOC0));

    // The window follows from the core type, never from the coordinate: on qsr.s1 the south D2D
    // ingress rows overlap legitimate config-window tiles, so range checks would misroute them.
    // LITERAL coordinates arrive with CoreType::UNSPECIFIED, so ask the descriptor what sits there.
    CoreType core_type = core.core_type;
    if (core_type == CoreType::UNSPECIFIED) {
        core_type = soc_descriptor_->get_coord_at(noc0_xy, CoordSystem::NOC0).core_type;
    }

    const auto x = static_cast<uint32_t>(noc0_xy.x);
    const auto y = static_cast<uint32_t>(noc0_xy.y);

    // Every branch logs the window it picked and the index within it. The wire carries the target
    // twice -- as (x, y) and encoded in the address -- and nothing downstream cross-checks them, so
    // when an access lands on the wrong core this line is what says whether the driver's choice of
    // window, its index, or the backend is at fault.
    if (core_type == CoreType::DRAM) {
        // The descriptor's DRAM channel map is the authority for which GDDR window a core belongs
        // to; LOGICAL x is the channel and LOGICAL y the subchannel.
        const CoreCoord logical = soc_descriptor_->translate_coord_to(
            CoreCoord(noc0_xy, CoreType::DRAM, CoordSystem::NOC0), CoordSystem::LOGICAL);
        const auto channel = static_cast<uint32_t>(logical.x);
        const uint64_t address = windows_.dram_address(channel, offset);
        log_debug(
            tt::LogUMD,
            "Grendel ATT: core ({}, {}) type {} -> Mimir GDDR window, channel {} (subchannel {}), "
            "offset 0x{:x} -> flat 0x{:x}",
            x,
            y,
            to_str(core_type),
            channel,
            logical.y,
            offset,
            address);
        return address;
    }

    if ((core_type == CoreType::TENSIX || core_type == CoreType::WORKER) && windows_.is_in_neo_grid(x, y)) {
        const uint64_t index = (y - windows_.neo_y_start) * windows_.neo_x_count + (x - windows_.neo_x_start);
        const uint64_t address = windows_.tensix_l1_address(x, y, offset);
        log_debug(
            tt::LogUMD,
            "Grendel ATT: core ({}, {}) type {} -> TensixNEO L1 window, slot {}, offset 0x{:x} -> flat 0x{:x}",
            x,
            y,
            to_str(core_type),
            index,
            offset,
            address);
        return address;
    }

    // Everything else (router, dispatch, ARC, PCIe, security, L2CPU, and Tensix tiles outside the
    // NEO grid) is reached through its per-tile config aperture.
    // config_address() validates the core is in the mesh, so it runs before the index is derived --
    // an out-of-mesh coordinate would underflow the subtraction below.
    const uint64_t address = windows_.config_address(x, y, offset);
    const uint64_t tile_id = (y - windows_.quasar_origin_y) * windows_.mesh_x_size + (x - windows_.quasar_origin_x);
    log_debug(
        tt::LogUMD,
        "Grendel ATT: core ({}, {}) type {} -> per-tile config window, tile_id {}, offset 0x{:x} -> flat 0x{:x}",
        x,
        y,
        to_str(core_type),
        tile_id,
        offset,
        address);
    return address;
}

}  // namespace tt::umd
