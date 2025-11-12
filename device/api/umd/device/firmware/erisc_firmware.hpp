/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <optional>
#include <vector>

#include "umd/device/utils/semver.hpp"

namespace tt::umd {

// ERISC FW version Required by UMD.
constexpr semver_t BH_ERISC_FW_SUPPORTED_VERSION_MIN = semver_t(1, 4, 1);
constexpr semver_t WH_ERISC_FW_SUPPORTED_VERSION_MIN = semver_t(6, 0, 0);
constexpr semver_t WH_ERISC_FW_ETH_BROADCAST_SUPPORTED_MIN = semver_t(6, 5, 0);
constexpr semver_t WH_ERISC_FW_ETH_BROADCAST_VIRTUAL_COORDS_MIN = semver_t(6, 8, 0);

const std::vector<std::pair<semver_t, semver_t>> WH_ERISC_FW_VERSION_MAP = {
    {{18, 2, 0}, {6, 6, 14}},
    {{18, 4, 0}, {6, 6, 15}},
    {{18, 6, 0}, {6, 7, 0}},
    {{18, 12, 0}, {6, 7, 1}},
    {{19, 0, 0}, {6, 7, 2}}};
const std::vector<std::pair<semver_t, semver_t>> WH_LEGACY_ERISC_FW_VERSION_MAP = {{{80, 17, 0}, {6, 6, 14}}};
const std::vector<std::pair<semver_t, semver_t>> BH_ERISC_FW_VERSION_MAP = {
    {{18, 5, 0}, {1, 4, 1}},
    {{18, 6, 0}, {1, 4, 2}},
    {{18, 9, 0}, {1, 5, 0}},
    {{18, 10, 0}, {1, 6, 0}},
    {{18, 12, 0}, {1, 7, 0}}};

// std::optional<semver_t> get_expected_erisc_fw_version_from_fw_bundle(semver_t fw_bundle_version, tt::ARCH arch) {
//     return std::nullopt;
// }
}  // namespace tt::umd
