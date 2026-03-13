// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

static constexpr std::uint32_t ETH_SPEED_ADDR = 0x7CC0C;

int main(int argc, char* argv[]) {
    cxxopts::Options options("eth_info", "Read information from connected ETH cores for each chip.");

    options.add_options()("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

        for (ChipId chip_id : cluster->get_target_device_ids()) {
            const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
            uint32_t num_eth_channels = soc_desc.get_num_eth_channels();

            std::cout << "Chip " << chip_id << " (" << num_eth_channels << " ETH channels):" << std::endl;

            for (uint32_t chan = 0; chan < num_eth_channels; chan++) {
                CoreCoord translated_coord = soc_desc.get_eth_core_for_channel(chan, CoordSystem::TRANSLATED);
                CoreCoord logical_coord = soc_desc.get_eth_core_for_channel(chan, CoordSystem::LOGICAL);

                uint32_t speed = 0;
                cluster->read_from_device(&speed, chip_id, translated_coord, ETH_SPEED_ADDR, sizeof(uint32_t));

                std::cout << "  ETH channel " << chan << " " << logical_coord.str() << " speed: " << speed << std::endl;
            }

            std::cout << std::endl;
        }
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error: {}", e.what());
        return 1;
    }

    return 0;
}
