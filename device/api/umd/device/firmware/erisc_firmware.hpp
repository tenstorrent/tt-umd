/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <picosha2.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "umd/device/utils/semver.hpp"

namespace tt::umd::erisc_firmware {

// ERISC FW versions required by UMD.
constexpr SemVer BH_MIN_ERISC_FW_SUPPORTED_VERSION = SemVer(1, 4, 1);
constexpr SemVer WH_MIN_ERISC_FW_SUPPORTED_VERSION = SemVer(6, 0, 0);
constexpr SemVer WH_MIN_ERISC_FW_ETH_BROADCAST_SUPPORTED = SemVer(6, 5, 0);
constexpr SemVer WH_MIN_ERISC_FW_ETH_BROADCAST_VIRTUAL_COORDS = SemVer(6, 8, 0);

// Maps firmware bundle versions to their corresponding ERISC firmware versions.
// Bundle versions between entries inherit the ERISC version from the previous entry.
static const std::vector<std::pair<SemVer, SemVer>> WH_ERISC_FW_VERSION_MAP = {
    {{80, 17, 0}, {6, 14, 0}},  // Legacy FW bundle version with major >= 80 is oldest.
    {{18, 2, 0}, {6, 14, 0}},
    {{18, 4, 0}, {6, 15, 0}},
    {{18, 6, 0}, {7, 0, 0}},
    {{18, 12, 0}, {7, 1, 0}},
    {{19, 0, 0}, {7, 2, 0}},
    {{19, 4, 0}, {7, 3, 0}},
    {{19, 4, 1}, {7, 2, 0}},
    {{19, 5, 0}, {7, 5, 0}}};
static const std::vector<std::pair<SemVer, SemVer>> BH_ERISC_FW_VERSION_MAP = {
    {{18, 5, 0}, {1, 4, 1}},
    {{18, 6, 0}, {1, 4, 2}},
    {{18, 9, 0}, {1, 5, 0}},
    {{18, 10, 0}, {1, 6, 0}},
    {{18, 12, 1}, {1, 7, 0}},
    {{19, 3, 0}, {1, 7, 1}}};

struct HashedAddressRange {
    size_t start_address;
    size_t size;
    std::string sha256_hash;
};

static const std::unordered_map<SemVer, HashedAddressRange> WH_ERISC_FW_HASHES = {
    {{7, 0, 0}, {0x2000, 0x6b5c, "3fb53365b7e07107f447b87faa3781558e3dbba0e942af2e54e985a0b64360c8"}},
    {{7, 1, 0}, {0x2000, 0x6a90, "7b3abf5258f1d95ffe0e6e69bf7638a31130607da3ba2474400e306967dddbbf"}},
    {{7, 2, 0}, {0x2000, 0x6b9c, "49983136ba696a83411e607f4fd7f1abdba6c650269e12904b2da71d19fdd1ee"}},
    {{7, 3, 0}, {0x2000, 0x6b38, "b2136da243d43af4a3181c65d135d0835e698786cc72d346e7578be2a7130ed6"}},
    {{7, 4, 0}, {0x2000, 0x6af4, "ee2e7b1b4b8c8fd798e582d645516a7d626495bc88429da1dac38e905cf54695"}},
    {{7, 5, 0}, {0x2000, 0x6bcc, "316d28873339992f5526e2994c2323d3b2bec6dcc04f3bdae52d9a9a5d351efa"}},
};

static const std::unordered_map<SemVer, HashedAddressRange> BH_ERISC_FW_HASHES = {
    {{1, 4, 2}, {0x72000, 0x8600, "c5385d26fc0aafa783cc5119711bff4c249ad869cd79ec03208cfa923ed26f70"}},
    {{1, 5, 0}, {0x72000, 0x86b4, "08c27a5084899d2cd92f3024365ad08695e6ce5bb512d0316f3380b78e15855f"}},
    {{1, 5, 1}, {0x70000, 0xa6b4, "b937deabb3d4525c5fa2910bcb62fa28097df3b647f69d0db5ef383fbe6ff7b2"}},
    {{1, 6, 0}, {0x70000, 0xa6b4, "b9b8fbc3d8204b02f1d32fade19cbc2abf2f7c4948d5901e25276efbc0865b0a"}},
    {{1, 6, 1}, {0x70000, 0xa6b4, "797d5f45828d71503ea597c890642778639cb204ae1c1ecc2d371ba6aa6ae369"}},
    {{1, 6, 2}, {0x70000, 0xa6b4, "0b8f858a44b4246ddb830cc91eca147044e0530a517007f0221f3b3fbb7b41c4"}},
    {{1, 7, 0}, {0x70000, 0xa6ec, "fe5620b007338f9c55854b1b76947c68dab63a5a1bfe8f4cbcfe1eb3620c4dc3"}},
};

}  // namespace tt::umd::erisc_firmware
