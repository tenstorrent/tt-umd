// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The ATT only exists on Quasar, which this build does not support: no resolver can be built.

#include "tt-umd/coordinates/att/att_resolver.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd::att {

namespace {

[[noreturn]] void throw_att_not_supported() {
    UMD_THROW(error::RuntimeError, "ATT address resolution is only used by Quasar, which is not supported.");
}

}  // namespace

EndpointResolver::EndpointResolver(const MapData& map) : map_(map) { throw_att_not_supported(); }

uint64_t EndpointResolver::resolve(tt_xy_pair, CoreType, uint64_t, uint64_t) const { throw_att_not_supported(); }

uint64_t resolve_core(const EndpointResolver&, const SocDescriptor&, const CoreCoord&, uint64_t, uint64_t) {
    throw_att_not_supported();
}

uint64_t resolve_translated(const EndpointResolver&, const SocDescriptor&, tt_xy_pair, uint64_t, uint64_t) {
    throw_att_not_supported();
}

std::optional<uint64_t> resolve_io_window(
    const EndpointResolver&, const SocDescriptor&, const CoreCoord&, WindowFlags, IoOrdering, uint64_t) {
    throw_att_not_supported();
}

}  // namespace tt::umd::att
