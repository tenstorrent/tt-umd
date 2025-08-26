// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/blackhole_firmware_versioner.h"

#include "umd/device/semver.hpp"

namespace tt::umd {

BlackholeFirmwareVersioner::BlackholeFirmwareVersioner(TTDevice* tt_device) : FirmwareVersioner(tt_device) {}

semver_t BlackholeFirmwareVersioner::get_firmware_version() { return semver_t(0, 0, 0); }

semver_t BlackholeFirmwareVersioner::get_minimum_compatible_firmware_version() { return semver_t(0, 0, 0); }

uint64_t BlackholeFirmwareVersioner::get_board_id() { return 0; }

}  // namespace tt::umd
