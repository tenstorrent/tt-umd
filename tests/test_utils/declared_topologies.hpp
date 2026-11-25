// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <map>

#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"

namespace tt::umd::test_utils {

// The two chips of the bh_x2 image declared as two single-chip boards, so that one chip can be selected without its
// board mate.
inline constexpr const char* DECLARED_BLACKHOLE_TWO_P150_BOARDS = R"(arch:
  0: blackhole
  1: blackhole
chips: {}
chip_unique_ids:
  0: 5550000
  1: 5550001
ethernet_connections:
  - [{chip: 0, chan: 2}, {chip: 1, chan: 9}]
  - [{chip: 0, chan: 3}, {chip: 1, chan: 8}]
chips_with_mmio:
  - 0: 0
  - 1: 1
harvesting:
  0: {noc_translation: true, harvest_mask: 8320, dram_harvesting_mask: 0, eth_harvesting_mask: 12288,
      pcie_harvesting_mask: 2}
  1: {noc_translation: true, harvest_mask: 272, dram_harvesting_mask: 0, eth_harvesting_mask: 12288,
      pcie_harvesting_mask: 2}
boards:
  - - board_id: 4398046511105
    - board_type: p150
    - chips: [0]
  - - board_id: 4398046511106
    - board_type: p150
    - chips: [1]
)";

// One unharvested chip, so that a SoC descriptor with fewer cores than the chip has still fits its harvesting.
inline constexpr const char* DECLARED_WORMHOLE_UNHARVESTED = R"(arch:
  0: wormhole_b0
chips:
  0: [0, 0, 0, 0]
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 0}
boards:
  - - board_id: 1649267441665
    - board_type: n150
    - chips: [0]
)";

inline constexpr const char* DECLARED_BLACKHOLE_UNHARVESTED = R"(arch:
  0: blackhole
chips: {}
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 0, dram_harvesting_mask: 0, eth_harvesting_mask: 0,
      pcie_harvesting_mask: 2}
boards:
  - - board_id: 4398046511105
    - board_type: p150
    - chips: [0]
)";

// A SoC descriptor the image's architecture ships in the repo whose cores differ from the built-in descriptor's, and
// how many cores of one type a device built from it has.
struct ShippedSocDescriptorCase {
    const char* cluster_yaml;
    const char* soc_descriptor_name;
    CoreType core_type;
    size_t cores;
};

inline const std::map<tt::ARCH, ShippedSocDescriptorCase>& shipped_soc_descriptors() {
    static const std::map<tt::ARCH, ShippedSocDescriptorCase> descriptors = {
        {tt::ARCH::WORMHOLE_B0, {DECLARED_WORMHOLE_UNHARVESTED, "wormhole_b0_1x1.yaml", CoreType::TENSIX, 1}},
        {tt::ARCH::BLACKHOLE, {DECLARED_BLACKHOLE_UNHARVESTED, "blackhole_140_arch_no_eth.yaml", CoreType::ETH, 0}},
    };
    return descriptors;
}

}  // namespace tt::umd::test_utils
