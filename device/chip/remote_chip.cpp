/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/remote_chip.h"

#include "logger.hpp"
#include "umd/device/chip/local_chip.h"

extern bool umd_use_noc1;

namespace tt::umd {

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor, eth_coord_t eth_chip_location, LocalChip* local_chip) :
    Chip(soc_descriptor),
    eth_chip_location_(eth_chip_location),
    remote_communication_(std::make_unique<RemoteCommunication>(local_chip)) {}

bool RemoteChip::is_mmio_capable() const { return false; }

void RemoteChip::read_from_device(
    tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size, const std::string& fallback_tlb) {
    // TODO: Fallback TLB is ignored for now, but it will be removed soon from the signature.
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
    log_assert(soc_descriptor_.arch != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");
    remote_communication_->wait_for_non_mmio_flush();
}

}  // namespace tt::umd
