/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/chip.h"

#include "assert.hpp"
#include "umd/device/architecture_implementation.h"
#include "umd/device/driver_atomics.h"
#include "umd/device/wormhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

Chip::Chip(tt_SocDescriptor soc_descriptor) : soc_descriptor_(soc_descriptor) {
    set_default_params(soc_descriptor.arch);
}

Chip::Chip(const ChipInfo chip_info, tt_SocDescriptor soc_descriptor) :
    chip_info_(chip_info), soc_descriptor_(soc_descriptor) {
    set_default_params(soc_descriptor.arch);
}

tt_SocDescriptor& Chip::get_soc_descriptor() { return soc_descriptor_; }

// TODO: This will be moved to LocalChip.
void Chip::set_default_params(ARCH arch) {
    auto architecture_implementation = architecture_implementation::create(arch);

    // Default initialize l1_address_params based on detected arch
    l1_address_params = architecture_implementation->get_l1_address_params();

    // Default initialize dram_address_params.
    dram_address_params = {0u};

    // Default initialize host_address_params based on detected arch
    host_address_params = architecture_implementation->get_host_address_params();

    // Default initialize eth_interface_params based on detected arch
    eth_interface_params = architecture_implementation->get_eth_interface_params();

    // Default initialize noc_params based on detected arch
    noc_params = architecture_implementation->get_noc_params();
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

void Chip::wait_eth_cores_training(const uint32_t timeout_ms) {}

void Chip::wait_dram_cores_training(const uint32_t timeout_ms) {}

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

void Chip::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type)valid;
    auto architecture_implementation = architecture_implementation::create(get_tt_device()->get_arch());
    write_to_device_reg(core, &valid_val, architecture_implementation->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    tt_driver_atomics::sfence();
}

void Chip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    for (const CoreCoord core : soc_descriptor_.get_cores(CoreType::TENSIX, CoordSystem::VIRTUAL)) {
        send_tensix_risc_reset(core, soft_resets);
    }
}

void Chip::set_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& selected_riscs) {
    uint32_t tensix_risc_state = 0x00000000;
    auto architecture_implementation = architecture_implementation::create(get_tt_device()->get_arch());
    read_from_device_reg(
        core, &tensix_risc_state, architecture_implementation->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    TensixSoftResetOptions set_selected_riscs = static_cast<TensixSoftResetOptions>(tensix_risc_state) | selected_riscs;
    send_tensix_risc_reset(core, set_selected_riscs);
}

void Chip::unset_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& selected_riscs) {
    uint32_t tensix_risc_state = 0x00000000;
    auto architecture_implementation = architecture_implementation::create(get_tt_device()->get_arch());
    read_from_device_reg(
        core, &tensix_risc_state, architecture_implementation->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    TensixSoftResetOptions set_selected_riscs =
        static_cast<TensixSoftResetOptions>(tensix_risc_state) & invert_selected_options(selected_riscs);
    send_tensix_risc_reset(core, set_selected_riscs);
}

uint32_t Chip::get_power_state_arc_msg(tt_DevicePowerState state) {
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
    if (soc_descriptor_.noc_translation_enabled) {
        if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
            if (core.core_type == CoreType::TENSIX || !umd_use_noc1) {
                return soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
            } else {
                return soc_descriptor_.translate_coord_to(core, CoordSystem::NOC1);
            }
        } else {
            return soc_descriptor_.translate_coord_to(core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
        }
    } else {
        return soc_descriptor_.translate_coord_to(core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
    }
}

}  // namespace tt::umd
