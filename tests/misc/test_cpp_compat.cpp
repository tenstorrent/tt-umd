// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

#include "common/cpp_compat.hpp"

using namespace tt::umd;

// Named rather than anonymous so these helpers don't collide with same-named ones in other tests
// when a unity build compiles them in one translation unit.
namespace cpp_compat_test {

enum class Rgb : uint8_t { RED, GREEN, BLUE };

enum class Defaulted { A, B };

enum Unscoped { X, Y };

// to_underlying yields the value in the enum's underlying type, for scoped and unscoped enums alike.
static_assert(to_underlying(Rgb::BLUE) == 2);
static_assert(std::is_same_v<decltype(to_underlying(Rgb::BLUE)), uint8_t>);
static_assert(std::is_same_v<decltype(to_underlying(Defaulted::A)), int>);
static_assert(to_underlying(Unscoped::Y) == 1);
static_assert(noexcept(to_underlying(Rgb::RED)));

// The case it exists for: an enumerator that has to become a number, here a bit position.
TEST(ToUnderlying, ComposesABitMask) {
    uint32_t mask = 0;
    mask |= 1u << to_underlying(Rgb::RED);
    mask |= 1u << to_underlying(Rgb::BLUE);
    EXPECT_EQ(mask, 0b101u);
}

}  // namespace cpp_compat_test
