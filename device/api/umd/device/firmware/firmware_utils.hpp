/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {
static semver_t fw_version_from_telemetry(const uint32_t telemetry_data);

semver_t get_firmware_version_util(TTDevice* tt_device);

std::optional<semver_t> get_expected_eth_firmware_version_from_firmware_bundle(
    semver_t fw_bundle_version, tt::ARCH arch);

}  // namespace tt::umd
