// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/reset/tsc_risc_reset.hpp"

#include "tt-umd/driver_atomics.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

bool selects(RiscType selected, RiscType riscs) { return (selected & riscs) != RiscType::NONE; }

uint64_t field_mask(uint32_t lsb, uint32_t width) { return ((uint64_t{1} << width) - 1) << lsb; }

}  // namespace

TscRiscReset::TscRiscReset(DeviceProtocol* device_protocol, const TscResetLayout& layout) :
    device_protocol_(device_protocol), layout_(layout) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "TscRiscReset requires a DeviceProtocol.");
}

uint64_t TscRiscReset::reset_bits(RiscType riscs) const {
    const bool all = selects(riscs, RiscType::ALL);
    uint64_t bits = 0;
    if (all || selects(riscs, RiscType::ALL_DATA_MOVEMENT)) {
        bits |= field_mask(layout_.dm_core_lsb, layout_.num_dm_cores);
    }
    for (uint32_t core = 0; core < layout_.num_dm_cores; core++) {
        if (selects(riscs, static_cast<RiscType>(static_cast<uint64_t>(RiscType::DM0) << core))) {
            bits |= uint64_t{1} << (layout_.dm_core_lsb + core);
        }
    }
    if (all || selects(riscs, RiscType::ALL_TRISCS)) {
        bits |= field_mask(layout_.risc_lsb, layout_.num_riscs);
    }
    for (uint32_t risc = 0; risc < layout_.num_riscs; risc++) {
        if (selects(riscs, static_cast<RiscType>(static_cast<uint64_t>(RiscType::NEO0_TRISC0) << risc))) {
            bits |= uint64_t{1} << (layout_.risc_lsb + risc);
        }
    }
    if (all || selects(riscs, RiscType::ALL_NEO_DMS_UNCORE | RiscType::NEO_DM_UNCORE)) {
        bits |= uint64_t{1} << layout_.dm_uncore_bit;
    }
    return bits;
}

void TscRiscReset::assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id) {
    std::lock_guard<std::mutex> lock(reset_register_mutex_);
    write_reset_ctrl(core, read_reset_ctrl(core, noc_id) & ~reset_bits(selected_riscs), noc_id);
}

void TscRiscReset::deassert_risc_reset(
    tt_xy_pair core, RiscType selected_riscs, [[maybe_unused]] bool staggered_start, NocId noc_id) {
    const uint64_t bits = reset_bits(selected_riscs);
    std::lock_guard<std::mutex> lock(reset_register_mutex_);
    write_reset_ctrl(core, read_reset_ctrl(core, noc_id) | bits, noc_id);

    const uint64_t dm_cores = bits & field_mask(layout_.dm_core_lsb, layout_.num_dm_cores);
    const uint64_t dm_uncore = bits & (uint64_t{1} << layout_.dm_uncore_bit);
    if (dm_cores != 0 || dm_uncore != 0) {
        write_overlay(core, layout_.overlay_uncore_soft_reset, 1, noc_id);
    }
    if (dm_cores != 0) {
        write_overlay(core, layout_.overlay_core_soft_reset, layout_.overlay_core_soft_reset_mask, noc_id);
    }
}

std::optional<RiscType> TscRiscReset::get_risc_reset_state(tt_xy_pair core, NocId noc_id) {
    const uint64_t released = read_reset_ctrl(core, noc_id);
    RiscType held = RiscType::NONE;
    for (uint32_t dm = 0; dm < layout_.num_dm_cores; dm++) {
        if ((released & (uint64_t{1} << (layout_.dm_core_lsb + dm))) == 0) {
            held |= static_cast<RiscType>(static_cast<uint64_t>(RiscType::DM0) << dm);
        }
    }
    for (uint32_t risc = 0; risc < layout_.num_riscs; risc++) {
        if ((released & (uint64_t{1} << (layout_.risc_lsb + risc))) == 0) {
            held |= static_cast<RiscType>(static_cast<uint64_t>(RiscType::NEO0_TRISC0) << risc);
        }
    }
    if ((released & (uint64_t{1} << layout_.dm_uncore_bit)) == 0) {
        held |= RiscType::NEO_DM_UNCORE;
    }
    return held;
}

uint64_t TscRiscReset::read_reset_ctrl(tt_xy_pair core, NocId noc_id) {
    uint64_t value = 0;
    device_protocol_->read_ctrl(&value, core, layout_.reset_ctrl, sizeof(value), noc_id, RegisterAccessWidth::BITS64);
    return value;
}

void TscRiscReset::write_reset_ctrl(tt_xy_pair core, uint64_t value, NocId noc_id) {
    device_protocol_->write_ctrl(&value, core, layout_.reset_ctrl, sizeof(value), noc_id, RegisterAccessWidth::BITS64);
    tt_driver_atomics::sfence();
}

void TscRiscReset::write_overlay(tt_xy_pair core, uint64_t addr, uint32_t value, NocId noc_id) {
    device_protocol_->write_ctrl(&value, core, addr, sizeof(value), noc_id, RegisterAccessWidth::BITS32);
    tt_driver_atomics::sfence();
}

}  // namespace tt::umd
