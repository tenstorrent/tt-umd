// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/remote_wormhole_tt_device.h"

namespace tt::umd {

RemoteWormholeTTDevice::RemoteWormholeTTDevice(LocalChip *local_chip, eth_coord_t target_chip) :
    WormholeTTDevice(),
    local_chip_(local_chip),
    target_chip_(target_chip),
    remote_communication_(std::make_unique<RemoteCommunication>(local_chip_)) {
    init_tt_device();
}

tt::ARCH RemoteWormholeTTDevice::get_arch() { return tt::ARCH::WORMHOLE_B0; }

void RemoteWormholeTTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->read_non_mmio(target_chip_, core, mem_ptr, addr, size);
}

void RemoteWormholeTTDevice::write_to_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->write_to_non_mmio(target_chip_, core, mem_ptr, addr, size);
}

}  // namespace tt::umd
