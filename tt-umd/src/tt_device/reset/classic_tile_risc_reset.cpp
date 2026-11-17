// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/reset/classic_tile_risc_reset.hpp"

#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"

namespace tt::umd {

ClassicTileRiscReset::ClassicTileRiscReset(
    DeviceProtocol* device_protocol, ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol), architecture_impl_(architecture_impl) {}

void ClassicTileRiscReset::assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id) {
    std::lock_guard<std::mutex> lock(reset_register_mutex_);
    const uint32_t value =
        read_reset_register(core, noc_id) | architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    write_reset_register(core, value, noc_id);
}

void ClassicTileRiscReset::deassert_risc_reset(
    tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    std::lock_guard<std::mutex> lock(reset_register_mutex_);
    uint32_t value = read_reset_register(core, noc_id) & ~architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (staggered_start) {
        value |= architecture_impl_->get_soft_reset_staggered_start();
    }
    write_reset_register(core, value, noc_id);
}

std::optional<RiscType> ClassicTileRiscReset::get_risc_reset_state(tt_xy_pair core, NocId noc_id) {
    return architecture_impl_->get_soft_reset_risc_type(read_reset_register(core, noc_id));
}

uint32_t ClassicTileRiscReset::read_reset_register(tt_xy_pair core, NocId noc_id) {
    uint32_t value = 0;
    device_protocol_->read_ctrl(
        &value,
        core,
        architecture_impl_->get_tensix_soft_reset_addr(),
        sizeof(value),
        noc_id,
        RegisterAccessWidth::BITS32);
    return value;
}

void ClassicTileRiscReset::write_reset_register(tt_xy_pair core, uint32_t value, NocId noc_id) {
    device_protocol_->write_ctrl(
        &value,
        core,
        architecture_impl_->get_tensix_soft_reset_addr(),
        sizeof(value),
        noc_id,
        RegisterAccessWidth::BITS32);
}

}  // namespace tt::umd
