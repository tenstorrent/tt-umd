// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

TEST(SimulatorHost, MappedTensixL1RoundTrip) {
    const char* simulator = std::getenv("TT_UMD_SIMULATOR");
    if (simulator == nullptr) {
        GTEST_SKIP() << "Set TT_UMD_SIMULATOR to a native libttsim library.";
    }
    auto device = TTSimTTDevice::create(simulator);
    const auto& soc = device->get_soc_descriptor();
    const auto cores = soc.get_cores(CoreType::TENSIX);
    ASSERT_FALSE(cores.empty());
    const auto core = soc.translate_coord_to(cores.front(), CoordSystem::TRANSLATED).to_pair();

    std::array<uint32_t, 256> expected{};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = 0x12340000u + static_cast<uint32_t>(i);
    }
    std::array<uint32_t, 256> actual{};
    constexpr uint64_t address = 0x10000;
    // Use the normal TLB path: direct tile_rd/tile_wr are not implemented for
    // Wormhole and Blackhole in the public simulator.
    device->write_to_device(expected.data(), core, address, sizeof(expected));
    device->read_from_device(actual.data(), core, address, sizeof(actual));
    EXPECT_EQ(actual, expected);
}

}  // namespace tt::umd
