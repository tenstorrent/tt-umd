// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/reset/tt_sim_risc_reset.hpp"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

TTSimRiscReset::TTSimRiscReset(
    TTDevice* device,
    ArchitectureImplementation* architecture_impl,
    bool inverted_reset_polarity,
    std::recursive_mutex& device_lock) :
    device_(device),
    architecture_impl_(architecture_impl),
    inverted_reset_polarity_(inverted_reset_polarity),
    device_lock_(device_lock) {
    UMD_ASSERT(
        device_ != nullptr && architecture_impl_ != nullptr,
        error::RuntimeError,
        "TTSimRiscReset requires a device and an ArchitectureImplementation.");
}

void TTSimRiscReset::update_reset_register(tt_xy_pair core, RiscType selected_riscs, bool set_bits) {
    std::lock_guard<std::recursive_mutex> lock(device_lock_);
    const uint64_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    const uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    // The coordinate arrives translated, so it is handed on as LITERAL.
    const CoreCoord translated_core{core.x, core.y};

    // Quasar holds its DM cores in reset with the bit cleared rather than set, and its register is
    // read and written as 64 bits.
    if (inverted_reset_polarity_) {
        uint64_t reset_value = 0;
        device_->read_from_device(&reset_value, translated_core, soft_reset_addr, sizeof(reset_value));
        reset_value = set_bits ? (reset_value & ~static_cast<uint64_t>(soft_reset_update))
                               : (reset_value | static_cast<uint64_t>(soft_reset_update));
        device_->write_to_device(&reset_value, translated_core, soft_reset_addr, sizeof(reset_value));
        return;
    }

    uint32_t reset_value = 0;
    device_->read_from_device(&reset_value, translated_core, soft_reset_addr, sizeof(reset_value));
    reset_value = set_bits ? (reset_value | soft_reset_update) : (reset_value & ~soft_reset_update);
    device_->write_to_device(&reset_value, translated_core, soft_reset_addr, sizeof(reset_value));
}

void TTSimRiscReset::assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, [[maybe_unused]] NocId noc_id) {
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    update_reset_register(core, selected_riscs, /*set_bits=*/true);
}

// A simulator starts its RISCs together, so staggered_start has nothing to drive.
void TTSimRiscReset::deassert_risc_reset(
    tt_xy_pair core, RiscType selected_riscs, [[maybe_unused]] bool staggered_start, [[maybe_unused]] NocId noc_id) {
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    update_reset_register(core, selected_riscs, /*set_bits=*/false);
}

std::optional<RiscType> TTSimRiscReset::get_risc_reset_state(
    [[maybe_unused]] tt_xy_pair core, [[maybe_unused]] NocId noc_id) {
    return std::nullopt;
}

}  // namespace tt::umd
