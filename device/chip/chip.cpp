/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/chip.hpp"

#include <chrono>
#include <cstdint>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "umd/device/utils/timeouts.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

Chip::Chip(SocDescriptor soc_descriptor) : soc_descriptor_(soc_descriptor) { set_default_params(soc_descriptor.arch); }

Chip::Chip(const ChipInfo chip_info, SocDescriptor soc_descriptor) :
    chip_info_(chip_info), soc_descriptor_(soc_descriptor) {
    set_default_params(soc_descriptor.arch);
}

SocDescriptor& Chip::get_soc_descriptor() { return soc_descriptor_; }

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
    wait_eth_cores_training();
    wait_dram_cores_training();
}

void Chip::wait_eth_cores_training(const std::chrono::milliseconds timeout_ms) {
    auto timeout_left = timeout_ms;
    const std::vector<CoreCoord> eth_cores = get_soc_descriptor().get_cores(CoreType::ETH);
    TTDevice* tt_device = get_tt_device();
    for (const CoreCoord& eth_core : eth_cores) {
        tt_xy_pair actual_eth_core = eth_core;
        if (get_tt_device()->get_arch() == tt::ARCH::WORMHOLE_B0) {
            // Translated space for ETH cores is different than NOC1 and wait_eth_core training is expecting NOC0
            // coordinates.
            actual_eth_core = soc_descriptor_.translate_coord_to(eth_core, CoordSystem::NOC0);
        } else {
            actual_eth_core = translate_chip_coord_to_translated(eth_core);
        }

        timeout_left -= tt_device->wait_eth_core_training(actual_eth_core, timeout_left);
    }
}

void Chip::wait_dram_cores_training(const std::chrono::milliseconds timeout_ms) {
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
    TT_ASSERT(
        soc_descriptor_.arch != tt::ARCH::BLACKHOLE,
        "enable_ethernet_queue is not supported on Blackhole architecture");
    uint32_t msg_success = 0x0;
    auto start = std::chrono::steady_clock::now();
    while (msg_success != 1) {
        if (std::chrono::steady_clock::now() - start > timeout_ms) {
            throw std::runtime_error(fmt::format(
                "Timed out after waiting {} milliseconds for for DRAM to finish training", timeout_ms.count()));
        }
        if (arc_msg(0xaa58, true, 0xFFFF, 0xFFFF, timeout::ARC_MESSAGE_TIMEOUT, &msg_success) == HANG_READ_VALUE) {
            break;
        }
    }
}

// TODO: Remove this API once we switch to the new one.
void Chip::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    TT_ASSERT(
        core.core_type == CoreType::TENSIX || core.core_type == CoreType::ETH,
        "Cannot control soft reset on a non-tensix or harvested core");
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = static_cast<uint32_t>(valid);
    get_tt_device()->set_risc_reset_state(translate_chip_coord_to_translated(core), valid_val);
}

// TODO: Remove this API once we switch to the new one.
void Chip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    for (const CoreCoord core : soc_descriptor_.get_cores(CoreType::TENSIX)) {
        send_tensix_risc_reset(core, soft_resets);
    }
}

RiscType Chip::get_risc_reset_state(CoreCoord core) {
    uint32_t soft_reset_current_state = get_tt_device()->get_risc_reset_state(translate_chip_coord_to_translated(core));
    return get_tt_device()->get_architecture_implementation()->get_soft_reset_risc_type(soft_reset_current_state);
}

void Chip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    uint32_t soft_reset_current_state = get_tt_device()->get_risc_reset_state(translate_chip_coord_to_translated(core));
    uint32_t soft_reset_update =
        get_tt_device()->get_architecture_implementation()->get_soft_reset_reg_value(selected_riscs);
    uint32_t soft_reset_new = soft_reset_current_state | soft_reset_update;
    log_debug(
        LogUMD,
        "Asserting RISC reset for core {}, current state: {}, update: {}, new state: {}",
        core,
        soft_reset_current_state,
        soft_reset_update,
        soft_reset_new);
    get_tt_device()->set_risc_reset_state(translate_chip_coord_to_translated(core), soft_reset_new);
}

void Chip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    uint32_t soft_reset_current_state = get_tt_device()->get_risc_reset_state(translate_chip_coord_to_translated(core));
    uint32_t soft_reset_update =
        get_tt_device()->get_architecture_implementation()->get_soft_reset_reg_value(selected_riscs);
    // The update variable should be applied in such a way that it clears the bits that are set in the selected_riscs.
    uint32_t soft_reset_new = soft_reset_current_state & ~soft_reset_update;
    uint32_t soft_reset_new_with_staggered_start =
        soft_reset_new |
        (staggered_start ? get_tt_device()->get_architecture_implementation()->get_soft_reset_staggered_start() : 0);
    log_debug(
        LogUMD,
        "Deasserting RISC reset for core {}, current state: {}, update: {}, new state: {}",
        core,
        soft_reset_current_state,
        soft_reset_update,
        soft_reset_new_with_staggered_start);
    get_tt_device()->set_risc_reset_state(
        translate_chip_coord_to_translated(core), soft_reset_new_with_staggered_start);
}

void Chip::assert_risc_reset(const RiscType selected_riscs) {
    for (const CoreCoord core : soc_descriptor_.get_cores(CoreType::TENSIX)) {
        assert_risc_reset(core, selected_riscs);
    }
}

void Chip::deassert_risc_reset(const RiscType selected_riscs, bool staggered_start) {
    for (const CoreCoord core : soc_descriptor_.get_cores(CoreType::TENSIX)) {
        deassert_risc_reset(core, selected_riscs, staggered_start);
    }
}

uint32_t Chip::get_power_state_arc_msg(DevicePowerState state) {
    uint32_t msg = wormhole::ARC_MSG_COMMON_PREFIX;
    switch (state) {
        case BUSY: {
            msg |= architecture_implementation::create(soc_descriptor_.arch)->get_arc_message_arc_go_busy();
            break;
        }
        case LONG_IDLE: {
            msg |= architecture_implementation::create(soc_descriptor_.arch)->get_arc_message_arc_go_long_idle();
            break;
        }
        case SHORT_IDLE: {
            msg |= architecture_implementation::create(soc_descriptor_.arch)->get_arc_message_arc_go_short_idle();
            break;
        }
        default:
            throw std::runtime_error("Unrecognized power state.");
    }
    return msg;
}

int Chip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
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
        get_tt_device()->get_arc_messenger()->send_message(msg_code, arc_msg_return_values, arg0, arg1, timeout_ms);

    if (return_3 != nullptr) {
        *return_3 = arc_msg_return_values[0];
    }

    if (return_4 != nullptr) {
        *return_4 = arc_msg_return_values[1];
    }

    return exit_code;
}

void Chip::set_power_state(DevicePowerState state) {
    int exit_code = 0;
    if (soc_descriptor_.arch == tt::ARCH::WORMHOLE_B0) {
        uint32_t msg = get_power_state_arc_msg(state);
        exit_code = arc_msg(wormhole::ARC_MSG_COMMON_PREFIX | msg, true, 0, 0);
    } else if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
        if (state == DevicePowerState::BUSY) {
            exit_code =
                get_tt_device()->get_arc_messenger()->send_message((uint32_t)blackhole::ArcMessageType::AICLK_GO_BUSY);
        } else {
            exit_code = get_tt_device()->get_arc_messenger()->send_message(
                (uint32_t)blackhole::ArcMessageType::AICLK_GO_LONG_IDLE);
        }
    }
    TT_ASSERT(exit_code == 0, "Failed to set power state to {} with exit code: {}", (int)state, exit_code);
    wait_for_aiclk_value(get_tt_device(), state);
}

tt_xy_pair Chip::translate_chip_coord_to_translated(const CoreCoord core) const {
    // Since NOC1 and translated coordinate space overlaps for Tensix cores on Blackhole,
    // Tensix cores are always used in translated space. Other cores are used either in
    // NOC1 or translated space depending on the umd_use_noc1 flag.
    // On Wormhole Tensix can use NOC1 space if umd_use_noc1 is set to true.
    if (soc_descriptor_.noc_translation_enabled && soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
        return soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    }

    return soc_descriptor_.translate_coord_to(core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
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
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms.count()) {
            log_warning(
                LogUMD,
                "Waiting for AICLK value to settle failed on timeout after {}. Expected to see {}, last value "
                "observed {}. This can be due to possible overheating of the chip or other issues. ASIC temperature: "
                "{}",
                timeout_ms,
                target_aiclk,
                aiclk,
                tt_device->get_asic_temperature());
            return;
        }
        aiclk = tt_device->get_clock();
    }
}

void Chip::noc_multicast_write(void* dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    // TODO: Support other core types once needed.
    if (core_start.core_type != CoreType::TENSIX || core_end.core_type != CoreType::TENSIX) {
        TT_THROW("noc_multicast_write is only supported for Tensix cores.");
    }
    get_tt_device()->noc_multicast_write(
        dst, size, translate_chip_coord_to_translated(core_start), translate_chip_coord_to_translated(core_end), addr);
}

}  // namespace tt::umd
