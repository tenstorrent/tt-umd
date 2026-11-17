// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/remote_communication_legacy_firmware.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

RemoteCommunicationLegacyFirmware::RemoteCommunicationLegacyFirmware(
    TTDevice* local_tt_device, EthCoord target_chip, SysmemManager* sysmem_manager) :
    RemoteCommunication(local_tt_device, sysmem_manager) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

void RemoteCommunicationLegacyFirmware::read_non_mmio(
    tt_xy_pair target_core,
    void* dest,
    uint64_t core_src,
    uint32_t size_in_bytes,
    const std::chrono::milliseconds timeout_ms) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

void RemoteCommunicationLegacyFirmware::write_to_non_mmio(
    tt_xy_pair target_core,
    const void* src,
    uint64_t core_dest,
    uint32_t size_in_bytes,
    bool broadcast,
    std::vector<int> broadcast_header,
    const std::chrono::milliseconds timeout_ms) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

void RemoteCommunicationLegacyFirmware::wait_for_non_mmio_flush(const std::chrono::milliseconds timeout_ms) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

std::optional<EthCoord> RemoteCommunicationLegacyFirmware::get_target_eth_coord() {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

}  // namespace tt::umd
