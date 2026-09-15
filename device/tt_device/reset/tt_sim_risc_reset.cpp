// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/reset/tt_sim_risc_reset.hpp"

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

TTSimRiscReset::TTSimRiscReset(
    ReadFn read,
    WriteFn write,
    ArchitectureImplementation* architecture_impl,
    bool qsr_reset_polarity,
    std::recursive_mutex& device_lock) :
    read_(std::move(read)),
    write_(std::move(write)),
    architecture_impl_(architecture_impl),
    qsr_reset_polarity_(qsr_reset_polarity),
    device_lock_(device_lock) {
    UMD_ASSERT(
        read_ && write_ && architecture_impl_ != nullptr,
        error::RuntimeError,
        "TTSimRiscReset requires the device I/O callables and an ArchitectureImplementation.");
}

void TTSimRiscReset::assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id) {
    std::lock_guard<std::recursive_mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    uint64_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (qsr_reset_polarity_) {
        uint64_t reset_value;
        read_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value &=
            ~(uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        write_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        read_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value |= soft_reset_update;
        write_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    }
}

void TTSimRiscReset::deassert_risc_reset(tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    std::lock_guard<std::recursive_mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    uint64_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);

    if (qsr_reset_polarity_) {
        uint64_t reset_value;
        read_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value |=
            (uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        write_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        read_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value &= ~soft_reset_update;
        write_(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    }
}

std::optional<RiscType> TTSimRiscReset::get_risc_reset_state(tt_xy_pair core, NocId noc_id) { return std::nullopt; }

}  // namespace tt::umd
