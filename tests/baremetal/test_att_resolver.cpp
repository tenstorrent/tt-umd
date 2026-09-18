// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "umd/device/coordinates/att/att_resolver.hpp"
#include "umd/device/coordinates/att/att_window.hpp"

using namespace tt::umd;

namespace {

// A map with one populated window, for exercising the checks the resolver makes on its input.
att::MapData map_with_worker_endpoints(const att::Window& window, att::Table<uint16_t> words) {
    att::MapData map{};
    map.windows[static_cast<size_t>(att::WindowClass::Worker)] = window;
    map.endpoint_words[static_cast<size_t>(att::WindowClass::Worker)] = words;
    return map;
}

// Two selector values, so a third endpoint row has no selector that can reach it.
constexpr att::Window TWO_SELECTOR_WINDOW{
    .compare = 0x1000,
    .mask_bits = 8,
    .endpoint_shift = 7,
    .endpoint_size = 1,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

}  // namespace

TEST(AttResolver, RejectsAnEndpointTableLongerThanItsSelectorField) {
    constexpr uint16_t words[] = {0x104, 0x105, 0x106};

    EXPECT_THROW(att::Resolver{map_with_worker_endpoints(TWO_SELECTOR_WINDOW, words)}, std::runtime_error);
}

TEST(AttResolver, RejectsAnEndpointTableThatNamesOneCoreTwice) {
    // A repeated coordinate leaves the core reachable through either slot, so a coordinate lookup
    // over the table cannot say which one the caller meant.
    constexpr uint16_t words[] = {0x104, 0x104};

    EXPECT_THROW(att::Resolver{map_with_worker_endpoints(TWO_SELECTOR_WINDOW, words)}, std::runtime_error);
}
