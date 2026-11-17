// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Remote chips are not supported by this build: this transport refuses construction.

#include "tt-umd/tt_device/remote_communication_legacy_firmware.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_legacy_remote_not_supported() {
    UMD_THROW(error::RuntimeError, "Remote devices are not supported.");
}

}  // namespace

RemoteCommunicationLegacyFirmware::RemoteCommunicationLegacyFirmware(
    TTDevice* local_tt_device, EthCoord target_chip, SysmemManager* sysmem_manager) :
    RemoteCommunication(local_tt_device, sysmem_manager),
    target_chip(target_chip),
    host_address_params_{},
    eth_interface_params_{},
    noc_params_{} {
    throw_legacy_remote_not_supported();
}

void RemoteCommunicationLegacyFirmware::read_non_mmio(
    tt_xy_pair, void*, uint64_t, uint32_t, const std::chrono::milliseconds) {
    throw_legacy_remote_not_supported();
}

void RemoteCommunicationLegacyFirmware::write_to_non_mmio(
    tt_xy_pair, const void*, uint64_t, uint32_t, bool, std::vector<int>, const std::chrono::milliseconds) {
    throw_legacy_remote_not_supported();
}

void RemoteCommunicationLegacyFirmware::wait_for_non_mmio_flush(const std::chrono::milliseconds) {
    throw_legacy_remote_not_supported();
}

std::optional<EthCoord> RemoteCommunicationLegacyFirmware::get_target_eth_coord() {
    throw_legacy_remote_not_supported();
}

}  // namespace tt::umd
