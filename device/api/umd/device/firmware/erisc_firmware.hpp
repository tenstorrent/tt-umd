/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <picosha2.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "umd/device/utils/semver.hpp"

namespace tt::umd::erisc_firmware {

// ERISC FW versions required by UMD.
constexpr semver_t BH_ERISC_FW_SUPPORTED_VERSION_MIN = semver_t(1, 4, 1);
constexpr semver_t WH_ERISC_FW_SUPPORTED_VERSION_MIN = semver_t(6, 0, 0);
constexpr semver_t WH_ERISC_FW_ETH_BROADCAST_SUPPORTED_MIN = semver_t(6, 5, 0);
constexpr semver_t WH_ERISC_FW_ETH_BROADCAST_VIRTUAL_COORDS_MIN = semver_t(6, 8, 0);

// Maps firmware bundle versions to their corresponding ERISC firmware versions.
// Bundle versions between entries inherit the ERISC version from the previous entry.
static const std::vector<std::pair<semver_t, semver_t>> WH_ERISC_FW_VERSION_MAP = {
    {{80, 17, 0}, {6, 14, 0}},  // Legacy FW bundle version with major >= 80 is oldest.
    {{18, 2, 0}, {6, 14, 0}},
    {{18, 4, 0}, {6, 15, 0}},
    {{18, 6, 0}, {7, 0, 0}},
    {{18, 12, 0}, {7, 1, 0}},
    {{19, 0, 0}, {7, 2, 0}}};
static const std::vector<std::pair<semver_t, semver_t>> BH_ERISC_FW_VERSION_MAP = {
    {{18, 5, 0}, {1, 4, 1}},
    {{18, 6, 0}, {1, 4, 2}},
    {{18, 9, 0}, {1, 5, 0}},
    {{18, 10, 0}, {1, 6, 0}},
    {{18, 12, 0}, {1, 7, 0}}};

struct HashedAddressRange {
    uint32_t start_address;
    uint32_t size;
    std::string sha256_hash;
};

static const std::unordered_map<semver_t, HashedAddressRange> WH_ERISC_FW_HASHES = {
    {{6, 0, 0}, {0x2000, 0x6c80, "972726fdb8b69fb242882b8c8b3e3da63714791626e623e16d82abb52f897c3a"}},
    {{6, 1, 0}, {0x2000, 0x6c80, "cce4222209071666661fb4b2074bfb3aa1e9925a54ba9178e93b8af4b79c2d1c"}},
    {{6, 2, 0}, {0x2000, 0x6c80, "3d0eda3e606afaee2e7b4a3ba58381a670ea99d35dec0bbadb8ead6840bd39aa"}},
    {{6, 3, 0}, {0x2000, 0x6c80, "f7438fe1e35581e11ef1c9122345cdce0b0520cb7df1ecd9b75b12da76e22747"}},
    {{6, 4, 0}, {0x2000, 0x7000, "248ea544be1fedec26e469d17acd670eb328065409b3f90758918151168ee29c"}},
    {{6, 5, 0}, {0x2000, 0x7000, "b875fd03b5a6f5e18094c545ba11ee9fc57899e4dfc4f8374978a8dc32aefeb9"}},
    {{6, 6, 0}, {0x2000, 0x7000, "df6d27685cf1f6c7bfb594851e3f867d349b8d14cf5ebad49144b76cdb229965"}},
    {{6, 7, 0}, {0x2000, 0x7000, "e9726bbd7f0a8dc392ea964b6f4914c703b27970c72abf10c9c643221c0658bd"}},
    {{6, 8, 0}, {0x2000, 0x7000, "ca3ab062ec4574ad391ca10883fb9dda5f97a3a3654afd9094673cd1c46afbeb"}},
    {{6, 9, 0}, {0x2000, 0x7000, "34c5e2033a7532814c6400f3ad52f7ab30bf1bd957b4b942d702bb81446f5e49"}},
    {{6, 10, 0}, {0x2000, 0x7000, "130ac50c37007f11b4b25a2be769d83e6102457d73d0593c2c96d3f7009bcec5"}},
    {{6, 11, 0}, {0x2000, 0x7000, "71534e0f947ff5bb8ddc921d84ece81ca281f4a3e93637c806cc4fccb076d25e"}},
    {{6, 12, 0}, {0x2000, 0x7000, "e8a3e2855c455f65cb7bec6964a7026fbfa32bcda1d19ea9e7780eec4924a676"}},
    {{6, 13, 0}, {0x2000, 0x7000, "dea44176fa00c3f3ccc14320e26f51db7aea302755b72284010343ea32c8822a"}},
    {{6, 14, 0}, {0x2000, 0x7000, "5932bafcd2bdf2e3e64defb628b97da8bd50f76afe8fd334a3eb5cd3c0fa8276"}},
    {{6, 15, 0}, {0x2000, 0x7000, "bb6d078d8ab3afb9e3b9c9d06a3d39d0fdb27299a2955fd977f759988acf94c8"}},
    {{7, 1, 0}, {0x2000, 0x7000, "76268f8d81a2cea29099730eb9ec166bbca2b812df3744b677b1d3e74d517161"}},
    {{7, 2, 0}, {0x2000, 0x7000, "7e3697077d76ea8e3f66f5b2ca61a19baf3be8b5435b096fb6bb3e52e7033f9d"}},
};

}  // namespace tt::umd::erisc_firmware
