// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/chip.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "tracy.hpp"
#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

// AICLK rarely settles on the exact target; accept any value within this percentage of the target.
constexpr double AICLK_TOLERANCE_PERCENT = 5.0;

Chip::Chip(tt::ARCH arch) { set_default_params(arch); }

Chip::Chip(const ChipInfo chip_info, tt::ARCH arch) : chip_info_(chip_info) { set_default_params(arch); }

// TODO: This will be moved to LocalChip.
void Chip::set_default_params(ARCH arch) {
    auto architecture_implementation = architecture_implementation::create(arch);

    // Default initialize l1_address_params based on detected arch.
    l1_address_params = architecture_implementation->get_l1_address_params();

    // Default initialize dram_address_params.
    dram_address_params = {0u};
}

void Chip::set_barrier_address_params(const BarrierAddressParams& barrier_address_params) {
    l1_address_params.tensix_l1_barrier_base = barrier_address_params.tensix_l1_barrier_base;
    l1_address_params.eth_l1_barrier_base = barrier_address_params.eth_l1_barrier_base;
    dram_address_params.DRAM_BARRIER_BASE = barrier_address_params.dram_barrier_base;
}

const ChipInfo& Chip::get_chip_info() { return chip_info_; }

void Chip::wait_chip_to_be_ready() {
    ZoneScopedC(tracy::Color::DarkGreen);
    wait_eth_cores_training();
    wait_dram_cores_training();
}

void Chip::wait_eth_cores_training(const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    auto timeout_left = timeout_ms;
    const std::vector<CoreCoord> eth_cores = get_soc_descriptor().get_cores(CoreType::ETH);
    TTDevice* tt_device = get_tt_device();
    for (const CoreCoord& eth_core : eth_cores) {
        tt_xy_pair actual_eth_core = get_soc_descriptor().translate_chip_coord_to_translated(eth_core);
        timeout_left -= tt_device->wait_eth_core_training(actual_eth_core, timeout_left);
    }
}

void Chip::wait_dram_cores_training(const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    TTDevice* tt_device = get_tt_device();
    const uint32_t dram_harvesting_mask = get_soc_descriptor().harvesting_masks.dram_harvesting_mask;
    const uint32_t chip_num_dram_channels = std::min(
        static_cast<size_t>(tt_device->get_architecture_implementation()->get_dram_banks_number()),
        get_soc_descriptor().get_dram_cores().size());
    for (int dram_channel = 0; dram_channel < chip_num_dram_channels; dram_channel++) {
        // Skip the check for harvested channels.
        if (dram_harvesting_mask & (1 << dram_channel)) {
            continue;
        }
        tt_device->wait_dram_channel_training(dram_channel);
    }
}

void Chip::enable_ethernet_queue(const std::chrono::milliseconds timeout_ms) {
    UMD_ASSERT(
        get_soc_descriptor().arch != tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        "enable_ethernet_queue is not supported on Blackhole architecture");
    uint32_t msg_success = 0x0;
    auto start = std::chrono::steady_clock::now();
    while (msg_success != 1) {
        if (std::chrono::steady_clock::now() - start > timeout_ms) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Timed out after waiting {} milliseconds for for DRAM to finish training.", timeout_ms.count()));
        }
        if (arc_msg(0xaa58, true, {0xFFFF, 0xFFFF}, timeout::ARC_MESSAGE_TIMEOUT, &msg_success) == HANG_READ_VALUE) {
            break;
        }
    }
}

RiscType Chip::get_risc_reset_state(CoreCoord core) {
    uint32_t soft_reset_current_state = get_tt_device()->get_risc_reset_state(core);
    return get_tt_device()->get_architecture_implementation()->get_soft_reset_risc_type(soft_reset_current_state);
}

void Chip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    get_tt_device()->assert_risc_reset(get_soc_descriptor().translate_chip_coord_to_translated(core), selected_riscs);
}

void Chip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    get_tt_device()->deassert_risc_reset(
        get_soc_descriptor().translate_chip_coord_to_translated(core), selected_riscs, staggered_start);
}

void Chip::assert_risc_reset(const RiscType selected_riscs) {
    ZoneScopedC(tracy::Color::DarkRed);
    for (const CoreCoord core : get_soc_descriptor().get_cores(CoreType::TENSIX)) {
        assert_risc_reset(core, selected_riscs);
    }
}

void Chip::deassert_risc_reset(const RiscType selected_riscs, bool staggered_start) {
    ZoneScopedC(tracy::Color::DarkGreen);
    for (const CoreCoord core : get_soc_descriptor().get_cores(CoreType::TENSIX)) {
        deassert_risc_reset(core, selected_riscs, staggered_start);
    }
}

uint32_t Chip::get_power_state_arc_msg(DevicePowerState state) {
    uint32_t msg = wormhole::ARC_MSG_COMMON_PREFIX;
    switch (state) {
        case BUSY: {
            msg |= architecture_implementation::create(get_soc_descriptor().arch)->get_arc_message_arc_go_busy();
            break;
        }
        case LONG_IDLE: {
            msg |= architecture_implementation::create(get_soc_descriptor().arch)->get_arc_message_arc_go_long_idle();
            break;
        }
        case SHORT_IDLE: {
            msg |= architecture_implementation::create(get_soc_descriptor().arch)->get_arc_message_arc_go_short_idle();
            break;
        }
        default:
            UMD_THROW(error::RuntimeError, "Unrecognized power state.");
    }
    return msg;
}

int Chip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    std::vector<uint32_t> arc_msg_return_values;
    if (return_3 != nullptr) {
        arc_msg_return_values.push_back(0);
    }

    if (return_4 != nullptr) {
        arc_msg_return_values.push_back(0);
    }

    uint32_t exit_code =
        get_tt_device()->get_arc_messenger()->send_message(msg_code, arc_msg_return_values, args, timeout_ms);

    if (return_3 != nullptr) {
        *return_3 = arc_msg_return_values[0];
    }

    if (return_4 != nullptr) {
        *return_4 = arc_msg_return_values[1];
    }

    return exit_code;
}

void Chip::advance_device_execution() {
    if (auto* td = get_tt_device()) {
        td->advance_device_execution();
    }
}

void Chip::set_power_state(DevicePowerState state) {
    ZoneScoped;
    int exit_code = 0;
    if (get_soc_descriptor().arch == tt::ARCH::WORMHOLE_B0) {
        uint32_t msg = get_power_state_arc_msg(state);
        exit_code = arc_msg(wormhole::ARC_MSG_COMMON_PREFIX | msg, true, {0, 0});
    } else if (get_soc_descriptor().arch == tt::ARCH::BLACKHOLE) {
        if (state == DevicePowerState::BUSY) {
            exit_code =
                get_tt_device()->get_arc_messenger()->send_message((uint32_t)blackhole::ArcMessageType::AICLK_GO_BUSY);
        } else {
            exit_code = get_tt_device()->get_arc_messenger()->send_message(
                (uint32_t)blackhole::ArcMessageType::AICLK_GO_LONG_IDLE);
        }
    }
    UMD_ASSERT(
        exit_code == 0,
        error::RuntimeError,
        fmt::format("Failed to set power state to {} with exit code: {}", (int)state, exit_code));
    wait_for_aiclk_value(get_tt_device(), state);
}

void Chip::wait_for_aiclk_value(
    TTDevice* tt_device, DevicePowerState power_state, const std::chrono::milliseconds timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    uint32_t target_aiclk = 0;
    if (power_state == DevicePowerState::BUSY) {
        target_aiclk = tt_device->get_max_clock_freq();
    } else if (power_state == DevicePowerState::LONG_IDLE) {
        target_aiclk = tt_device->get_min_clock_freq();
    }
    uint32_t aiclk = tt_device->get_clock();
    while (aiclk != target_aiclk) {
        if (is_within_percentage(aiclk, target_aiclk, AICLK_TOLERANCE_PERCENT)) {
            log_warning(
                LogUMD,
                "AICLK settled at {} MHz, within {}% of the requested {} MHz but not an exact match. Proceeding.",
                aiclk,
                AICLK_TOLERANCE_PERCENT,
                target_aiclk);
            return;
        }
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms.count()) {
            auto* telemetry = tt_device->get_arc_telemetry_reader();
            std::string arb_max_info;
            if (telemetry != nullptr && telemetry->is_entry_available(TelemetryTag::AICLK_ARB_MAX)) {
                const uint32_t arb_max = telemetry->read_entry(TelemetryTag::AICLK_ARB_MAX);
                const uint32_t arb_freq = arb_max & 0xFFFF;
                const uint32_t arb_idx = (arb_max >> 16) & 0xFFFF;
                arb_max_info = fmt::format(", AICLK clamped by max-arbiter index {} at {} MHz", arb_idx, arb_freq);
            }
            log_warning(
                LogUMD,
                "Waiting for AICLK value to settle failed on timeout after {}. Expected to see {}, last value "
                "observed {}. This can be due to possible overheating of the chip or other issues. ASIC temperature: "
                "{}{}",
                timeout_ms.count(),
                target_aiclk,
                aiclk,
                tt_device->get_asic_temperature(),
                arb_max_info);
            if (telemetry != nullptr && telemetry->is_entry_available(TelemetryTag::UPDATE_TELEM_SPEED)) {
                const uint32_t update_telem_speed_ms = telemetry->read_entry(TelemetryTag::UPDATE_TELEM_SPEED);
                if (timeout_ms.count() <= update_telem_speed_ms) {
                    log_warning(
                        LogUMD,
                        "AICLK timeout ({} ms) is not larger than the telemetry update interval ({} ms); the "
                        "observed AICLK may be a stale telemetry value. Consider increasing AICLK_TIMEOUT.",
                        timeout_ms.count(),
                        update_telem_speed_ms);
                }
            }
            return;
        }
        aiclk = tt_device->get_clock();
    }
}

void Chip::noc_multicast_write(const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    // TODO: Support other core types once needed.
    if (core_start.core_type != CoreType::TENSIX || core_end.core_type != CoreType::TENSIX) {
        UMD_THROW(error::RuntimeError, "noc_multicast_write is only supported for Tensix cores.");
    }
    get_tt_device()->noc_multicast_write(
        src,
        size,
        get_soc_descriptor().translate_chip_coord_to_translated(core_start),
        get_soc_descriptor().translate_chip_coord_to_translated(core_end),
        addr);
}

void Chip::noc_multicast_write(const void* src, size_t size, uint64_t addr) {
    get_tt_device()->noc_multicast_write(src, size, addr);
}

}  // namespace tt::umd
