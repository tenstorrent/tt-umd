/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/semver.hpp"
#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {
static semver_t fw_version_from_telemetry(const uint32_t telemetry_data);

semver_t get_firmware_version_util(TTDevice* tt_device);

}  // namespace tt::umd
