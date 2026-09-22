// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/reset/risc_reset_implementation.hpp"

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

ClassicTileRiscReset::ClassicTileRiscReset(
    DeviceProtocol* device_protocol, ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol), architecture_impl_(architecture_impl) {
    UMD_ASSERT(
        device_protocol_ != nullptr && architecture_impl_ != nullptr,
        error::RuntimeError,
        "ClassicTileRiscReset requires a DeviceProtocol and an ArchitectureImplementation.");
}

void ClassicTileRiscReset::assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id) {
    const uint32_t current_state = read_reset_register(core, noc_id);
    write_reset_register(core, current_state | architecture_impl_->get_soft_reset_reg_value(selected_riscs), noc_id);
}

void ClassicTileRiscReset::deassert_risc_reset(
    tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    const uint32_t current_state = read_reset_register(core, noc_id);
    uint32_t new_state = current_state & ~architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    new_state |= staggered_start ? architecture_impl_->get_soft_reset_staggered_start() : 0;
    write_reset_register(core, new_state, noc_id);
}

std::optional<RiscType> ClassicTileRiscReset::get_risc_reset_state(tt_xy_pair core, NocId noc_id) {
    return architecture_impl_->get_soft_reset_risc_type(read_reset_register(core, noc_id));
}

uint32_t ClassicTileRiscReset::read_reset_register(tt_xy_pair core, NocId noc_id) {
    uint32_t value = 0;
    device_protocol_->read_ctrl(&value, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(value), noc_id);
    return value;
}

void ClassicTileRiscReset::write_reset_register(tt_xy_pair core, uint32_t value, NocId noc_id) {
    device_protocol_->write_ctrl(&value, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(value), noc_id);
    tt_driver_atomics::sfence();
}

}  // namespace tt::umd
