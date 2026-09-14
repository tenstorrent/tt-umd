// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/coordinates/att/att_resolver.hpp"
#include "umd/device/coordinates/att/att_window.hpp"
#include "umd/device/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
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

namespace {

// The in-repo Quasar descriptor places its workers on the same grid as the qsr.s1 emulation model,
// so it exercises the descriptor-facing path over real coordinates.
SocDescriptor quasar_soc_descriptor() {
    return SocDescriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_32_arch.yaml")),
        {.noc_translation_enabled = false});
}

}  // namespace

TEST(AttResolveCore, ResolvesATypedCoreCoord) {
    const SocDescriptor soc_descriptor = quasar_soc_descriptor();
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);
    const CoreCoord core(2, 2, CoreType::TENSIX, CoordSystem::NOC0);

    EXPECT_EQ(att::resolve_core(resolver, soc_descriptor, core, 0x1000, 4), 0x10000001000ULL);
}

TEST(AttResolveCore, ResolvesAnUntypedLiteralCoordinateLikeItsTypedForm) {
    const SocDescriptor soc_descriptor = quasar_soc_descriptor();
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);

    // A client sends a coordinate it has already translated, carrying no core type. Both roles must
    // reach the same address or a client-mode access lands on a different core than a host one.
    const CoreCoord literal(2, 2, CoreType::UNSPECIFIED, CoordSystem::LITERAL);
    const CoreCoord typed(2, 2, CoreType::TENSIX, CoordSystem::NOC0);

    EXPECT_EQ(
        att::resolve_core(resolver, soc_descriptor, literal, 0x1000, 4),
        att::resolve_core(resolver, soc_descriptor, typed, 0x1000, 4));
}
