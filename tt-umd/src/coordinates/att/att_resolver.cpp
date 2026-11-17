// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/coordinates/att/att_resolver.hpp"

#include <cstdint>
#include <optional>

#include "common/unsupported.hpp"

namespace tt::umd::att {

namespace {

constexpr const char* ATT = "Address translation (Quasar)";

}  // namespace

EndpointResolver::EndpointResolver(const MapData& map) { UMD_NOT_SUPPORTED(ATT); }

uint64_t EndpointResolver::resolve(tt_xy_pair core, CoreType core_type, uint64_t offset, uint64_t size) const {
    UMD_NOT_SUPPORTED(ATT);
}

uint64_t resolve_core(
    const EndpointResolver& resolver,
    const SocDescriptor& soc_descriptor,
    const CoreCoord& core,
    uint64_t offset,
    uint64_t size) {
    UMD_NOT_SUPPORTED(ATT);
}

uint64_t resolve_translated(
    const EndpointResolver& resolver,
    const SocDescriptor& soc_descriptor,
    tt_xy_pair core,
    uint64_t offset,
    uint64_t size) {
    UMD_NOT_SUPPORTED(ATT);
}

std::optional<uint64_t> resolve_io_window(
    const EndpointResolver& resolver,
    const SocDescriptor& soc_descriptor,
    const CoreCoord& core,
    WindowFlags flags,
    IoOrdering ordering,
    uint64_t offset) {
    UMD_NOT_SUPPORTED(ATT);
}

}  // namespace tt::umd::att
