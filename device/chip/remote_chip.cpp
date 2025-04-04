/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/remote_chip.h"

#include "umd/device/chip/local_chip.h"

namespace tt::umd {

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor, eth_coord_t eth_chip_location, LocalChip* local_chip) :
    Chip(soc_descriptor),
    eth_chip_location_(eth_chip_location),
    remote_communication_(std::make_unique<RemoteCommunication>(local_chip)) {}

bool RemoteChip::is_mmio_capable() const { return false; }

void RemoteChip::read_from_device(
    tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size, const std::string& fallback_tlb) {
    // TODO: Fallback TLB is ignored for now, but it will be removed soon from the signature.
    // TODO: This translation should go away when we start using CoreCoord everywhere.
    auto translated_core = get_soc_descriptor().translate_coord_to(core, CoordSystem::VIRTUAL, CoordSystem::TRANSLATED);
    remote_communication_->read_non_mmio(eth_chip_location_, translated_core, dest, l1_src, size);
}
}  // namespace tt::umd
