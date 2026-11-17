// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/reset/classic_tile_risc_reset.hpp"

#include <cstdint>
#include <optional>

#include "common/unsupported.hpp"

namespace tt::umd {

ClassicTileRiscReset::ClassicTileRiscReset(
    DeviceProtocol* device_protocol, ArchitectureImplementation* architecture_impl) {
    UMD_NOT_IMPLEMENTED();
}

void ClassicTileRiscReset::assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void ClassicTileRiscReset::deassert_risc_reset(
    tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

std::optional<RiscType> ClassicTileRiscReset::get_risc_reset_state(tt_xy_pair core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

uint32_t ClassicTileRiscReset::read_reset_register(tt_xy_pair core, NocId noc_id) { UMD_NOT_IMPLEMENTED(); }

void ClassicTileRiscReset::write_reset_register(tt_xy_pair core, uint32_t value, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
