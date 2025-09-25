/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/chip.hpp"

#include <cstdint>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"

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

    // Default initialize l1_address_params based on detected arch
    l1_address_params = architecture_implementation->get_l1_address_params();

    // Default initialize dram_address_params.
    dram_address_params = {0u};
}

void Chip::set_barrier_address_params(const barrier_address_params& barrier_address_params_) {
    l1_address_params.tensix_l1_barrier_base = barrier_address_params_.tensix_l1_barrier_base;
    l1_address_params.eth_l1_barrier_base = barrier_address_params_.eth_l1_barrier_base;
    dram_address_params.DRAM_BARRIER_BASE = barrier_address_params_.dram_barrier_base;
}

const ChipInfo& Chip::get_chip_info() { return chip_info_; }

void Chip::wait_chip_to_be_ready() {
    wait_eth_cores_training();
    wait_dram_cores_training();
}

void Chip::wait_eth_cores_training(const uint32_t timeout_ms) {
    uint32_t timeout_left = timeout_ms;
    const std::vector<CoreCoord> eth_cores = get_soc_descriptor().get_cores(CoreType::ETH);
    TTDevice* tt_device = get_tt_device();
    for (const CoreCoord& eth_core : eth_cores) {
        tt_xy_pair actual_eth_core = eth_core;
        if (chip_info_.board_type == BoardType::UBB) {
            // TODO issue 1208: figure out why translated ETH don't work on UBB
            actual_eth_core =
                soc_descriptor_.translate_coord_to(eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        } else {
            actual_eth_core = translate_chip_coord_to_translated(eth_core);
        }

        timeout_left -= tt_device->wait_eth_core_training(actual_eth_core, timeout_left);
    }
}

void Chip::wait_dram_cores_training(const uint32_t timeout_ms) {
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

void Chip::enable_ethernet_queue(int timeout_s) {
    TT_ASSERT(
        soc_descriptor_.arch != tt::ARCH::BLACKHOLE,
        "enable_ethernet_queue is not supported on Blackhole architecture");
    uint32_t msg_success = 0x0;
    auto timeout_seconds = std::chrono::seconds(timeout_s);
    auto start = std::chrono::system_clock::now();
    while (msg_success != 1) {
        if (std::chrono::system_clock::now() - start > timeout_seconds) {
            throw std::runtime_error(
                fmt::format("Timed out after waiting {} seconds for for DRAM to finish training", timeout_s));
        }
        if (arc_msg(0xaa58, true, 0xFFFF, 0xFFFF, 1000, &msg_success) == HANG_READ_VALUE) {
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
        LogSiliconDriver,
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
        LogSiliconDriver,
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
    uint32_t timeout_ms,
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

void Chip::wait_for_aiclk_value(TTDevice* tt_device, DevicePowerState power_state, const uint32_t timeout_ms) {
    auto start = std::chrono::system_clock::now();
    uint32_t target_aiclk = 0;
    if (power_state == DevicePowerState::BUSY) {
        target_aiclk = tt_device->get_max_clock_freq();
    } else if (power_state == DevicePowerState::LONG_IDLE) {
        target_aiclk = tt_device->get_min_clock_freq();
    }
    uint32_t aiclk = tt_device->get_clock();
    while (aiclk != target_aiclk) {
        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms) {
            log_warning(
                LogSiliconDriver,
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

}  // namespace tt::umd
