/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/remote_chip.h"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/chip/local_chip.h"
#include "umd/device/wormhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor, eth_coord_t eth_chip_location, LocalChip* local_chip) :
    Chip(soc_descriptor),
    eth_chip_location_(eth_chip_location),
    remote_communication_(std::make_unique<RemoteCommunication>(local_chip)),
    local_chip_(local_chip) {
    TT_ASSERT(soc_descriptor_.arch != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");
}

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor, ChipInfo chip_info) : Chip(chip_info, soc_descriptor) {}

bool RemoteChip::is_mmio_capable() const { return false; }

void RemoteChip::start_device() {}

void RemoteChip::close_device() {}

void RemoteChip::write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) {
    auto translated_core = translate_chip_coord_virtual_to_translated(core);
    remote_communication_->write_to_non_mmio(eth_chip_location_, translated_core, src, l1_dest, size);
}

void RemoteChip::read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) {
    auto translated_core = translate_chip_coord_virtual_to_translated(core);
    remote_communication_->read_non_mmio(eth_chip_location_, translated_core, dest, l1_src, size);
}

// TODO: This translation should go away when we start using CoreCoord everywhere.
tt_xy_pair RemoteChip::translate_chip_coord_virtual_to_translated(const tt_xy_pair core) {
    CoreCoord core_coord = get_soc_descriptor().get_coord_at(core, CoordSystem::VIRTUAL);
    // Since NOC1 and translated coordinate space overlaps for Tensix cores on Blackhole,
    // Tensix cores are always used in translated space. Other cores are used either in
    // NOC1 or translated space depending on the umd_use_noc1 flag.
    // On Wormhole Tensix can use NOC1 space if umd_use_noc1 is set to true.
    if (get_soc_descriptor().noc_translation_enabled) {
        if (get_soc_descriptor().arch == tt::ARCH::BLACKHOLE) {
            if (core_coord.core_type == CoreType::TENSIX || !umd_use_noc1) {
                return get_soc_descriptor().translate_coord_to(core_coord, CoordSystem::TRANSLATED);
            } else {
                return get_soc_descriptor().translate_coord_to(core_coord, CoordSystem::NOC1);
            }
        } else {
            return get_soc_descriptor().translate_coord_to(
                core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
        }
    } else {
        return get_soc_descriptor().translate_coord_to(
            core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
    }
}

void RemoteChip::wait_for_non_mmio_flush() {
    TT_ASSERT(soc_descriptor_.arch != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");
    remote_communication_->wait_for_non_mmio_flush();
}

int RemoteChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    auto arc_core = soc_descriptor_.get_cores(CoreType::ARC).at(0);
    return remote_communication_->arc_msg(
        eth_chip_location_, arc_core, msg_code, wait_for_done, arg0, arg1, timeout_ms, return_3, return_4);
}

void RemoteChip::l1_membar(const std::unordered_set<tt::umd::CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<tt::umd::CoreCoord>& cores, const uint32_t noc_port) {
    wait_for_non_mmio_flush();
}

void RemoteChip::dram_membar(const std::unordered_set<uint32_t>& channels, const uint32_t noc_port) {
    wait_for_non_mmio_flush();
}

void RemoteChip::deassert_risc_resets() { local_chip_->deassert_risc_resets(); }

void RemoteChip::set_power_state(tt_DevicePowerState state) {
    if (soc_descriptor_.arch == tt::ARCH::WORMHOLE_B0) {
        uint32_t msg = get_power_state_arc_msg(state);
        int exit_code = arc_msg(wormhole::ARC_MSG_COMMON_PREFIX | msg, true, 0, 0);
        TT_ASSERT(exit_code == 0, "Failed to set power state to {} with exit code: {}", (int)state, exit_code);
    } else if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
        throw std::runtime_error("set_power_state not supported for remote chips on Blackhole.");
    }
}

int RemoteChip::get_clock() { return local_chip_->get_clock(); }

}  // namespace tt::umd
