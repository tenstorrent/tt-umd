// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/enum_array.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt::umd;

namespace {

enum class Rgb : uint8_t { RED, GREEN, BLUE, COUNT };

enum class Single { ONLY, COUNT };

// Breaks the contract on purpose: an enumerator after COUNT, which is what at() exists to catch.
enum class Leaky { A, B, COUNT, PAST_COUNT };

enum class NoCount { A, B };

enum Unscoped { X, Y, COUNT };

struct StaticCount {
    static constexpr int COUNT = 3;
};

}  // namespace

// to_underlying yields the underlying type, for scoped and unscoped enums alike.
static_assert(to_underlying(Rgb::BLUE) == 2);
static_assert(std::is_same_v<decltype(to_underlying(Rgb::BLUE)), uint8_t>);
static_assert(std::is_same_v<decltype(to_underlying(Leaky::A)), int>);
static_assert(to_underlying(Unscoped::Y) == 1);
static_assert(noexcept(to_underlying(Rgb::RED)));

// The trait admits exactly: an enum class with a COUNT enumerator.
static_assert(is_enum_array_index_v<Rgb>);
static_assert(is_enum_array_index_v<Single>);
static_assert(is_enum_array_index_v<Leaky>);
static_assert(!is_enum_array_index_v<NoCount>);
static_assert(!is_enum_array_index_v<Unscoped>);
static_assert(!is_enum_array_index_v<StaticCount>);
static_assert(!is_enum_array_index_v<int>);
static_assert(!is_enum_array_index_v<void>);

// The size follows the enum, not a hand-maintained constant.
static_assert(EnumArray<int, Rgb>::SIZE == 3);
static_assert(EnumArray<int, Rgb>::size() == 3);
static_assert(EnumArray<int, Single>::SIZE == 1);
static_assert(EnumArray<int, tt::CoreType>::SIZE == static_cast<size_t>(tt::CoreType::COUNT));

// Element-wise construction needs exactly one convertible argument per enumerator.
static_assert(std::is_constructible_v<EnumArray<int, Rgb>, int, int, int>);
static_assert(!std::is_constructible_v<EnumArray<int, Rgb>, int, int>);
static_assert(!std::is_constructible_v<EnumArray<int, Rgb>, int, int, int, int>);
static_assert(!std::is_constructible_v<EnumArray<int, Rgb>, int, std::string, int>);

// With a single enumerator the element-wise constructor must not hijack copy and move.
static_assert(std::is_copy_constructible_v<EnumArray<int, Single>>);
static_assert(std::is_nothrow_move_constructible_v<EnumArray<int, Single>>);
static_assert(std::is_trivially_copyable_v<EnumArray<int, Rgb>>);

// Usable in constant expressions.
constexpr EnumArray<int, Rgb> CONSTEXPR_RGB{10, 20, 30};
static_assert(CONSTEXPR_RGB[Rgb::RED] == 10);
static_assert(CONSTEXPR_RGB[Rgb::GREEN] == 20);
static_assert(CONSTEXPR_RGB[Rgb::BLUE] == 30);
static_assert(CONSTEXPR_RGB.at(Rgb::BLUE) == 30);
static_assert(CONSTEXPR_RGB == EnumArray<int, Rgb>{10, 20, 30});
static_assert(CONSTEXPR_RGB != EnumArray<int, Rgb>{10, 20, 31});

TEST(EnumArray, DefaultConstructionValueInitialises) {
    EnumArray<int, Rgb> counts;
    EXPECT_EQ(counts[Rgb::RED], 0);
    EXPECT_EQ(counts[Rgb::GREEN], 0);
    EXPECT_EQ(counts[Rgb::BLUE], 0);
}

TEST(EnumArray, ElementWiseConstructionFollowsDeclarationOrder) {
    const EnumArray<std::string, Rgb> names{"red", "green", "blue"};
    EXPECT_EQ(names[Rgb::RED], "red");
    EXPECT_EQ(names[Rgb::GREEN], "green");
    EXPECT_EQ(names[Rgb::BLUE], "blue");
}

TEST(EnumArray, SubscriptWritesThrough) {
    EnumArray<int, Rgb> counts;
    counts[Rgb::GREEN] = 7;
    counts[Rgb::GREEN] += 1;
    EXPECT_EQ(counts[Rgb::GREEN], 8);
    EXPECT_EQ(counts[Rgb::RED], 0);
    EXPECT_EQ(counts[Rgb::BLUE], 0);
}

// Value iteration: a range-for over the array visits the values in enumerator order, and a non-const
// range-for writes through.
TEST(EnumArray, IterationOverValues) {
    EnumArray<int, Rgb> values{1, 2, 3};
    const std::vector<int> visited(values.begin(), values.end());
    EXPECT_EQ(visited, (std::vector<int>{1, 2, 3}));

    int sum = 0;
    for (const int value : values) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);

    for (int& value : values) {
        value *= 10;
    }
    EXPECT_EQ(values, (EnumArray<int, Rgb>{10, 20, 30}));
}

// Key iteration: keys() lists every enumerator in declaration order, so a loop can pair each key with its
// value without a single cast.
TEST(EnumArray, IterationOverKeys) {
    static_assert(EnumArray<int, Rgb>::keys().size() == 3);
    static_assert(EnumArray<int, Rgb>::keys()[0] == Rgb::RED);
    static_assert(EnumArray<int, Rgb>::keys()[1] == Rgb::GREEN);
    static_assert(EnumArray<int, Rgb>::keys()[2] == Rgb::BLUE);

    const EnumArray<std::string, Rgb> names{"red", "green", "blue"};
    std::string joined;
    for (const Rgb key : names.keys()) {
        joined += std::to_string(to_underlying(key)) + "=" + names[key] + " ";
    }
    EXPECT_EQ(joined, "0=red 1=green 2=blue ");

    EnumArray<int, Rgb> squares;
    for (const Rgb key : squares.keys()) {
        squares[key] = to_underlying(key) * to_underlying(key);
    }
    EXPECT_EQ(squares, (EnumArray<int, Rgb>{0, 1, 4}));
}

TEST(EnumArray, FillOverwritesEveryElement) {
    EnumArray<int, Rgb> values{1, 2, 3};
    values.fill(9);
    EXPECT_EQ(values, (EnumArray<int, Rgb>{9, 9, 9}));
}

TEST(EnumArray, EqualityIsElementWise) {
    const EnumArray<int, Rgb> a{1, 2, 3};
    EnumArray<int, Rgb> b{1, 2, 3};
    EXPECT_EQ(a, b);
    b[Rgb::BLUE] = 4;
    EXPECT_NE(a, b);
}

TEST(EnumArray, CopyAndMoveOfSingleElementArray) {
    EnumArray<std::string, Single> original{"only"};
    EnumArray<std::string, Single> copy(original);
    EXPECT_EQ(copy[Single::ONLY], "only");
    EnumArray<std::string, Single> moved(std::move(original));
    EXPECT_EQ(moved[Single::ONLY], "only");
}

TEST(EnumArray, AtThrowsForEnumeratorPastCount) {
    EnumArray<int, Leaky> values{1, 2};
    EXPECT_EQ(values.at(Leaky::A), 1);
    EXPECT_EQ(values.at(Leaky::B), 2);
    EXPECT_THROW(values.at(Leaky::COUNT), std::out_of_range);
    EXPECT_THROW(values.at(Leaky::PAST_COUNT), std::out_of_range);
    EXPECT_THROW(std::as_const(values).at(Leaky::PAST_COUNT), std::out_of_range);
}

// CoreType is the one enum in the codebase with a COUNT sentinel, and it declares UNSPECIFIED after it.
// Every real enumerator indexes; UNSPECIFIED is outside the array and at() says so.
TEST(EnumArray, WorksWithCoreType) {
    EnumArray<int, tt::CoreType> per_core_type;
    per_core_type[tt::CoreType::TENSIX] = 140;
    per_core_type[tt::CoreType::DRAM] = 8;
    EXPECT_EQ(per_core_type[tt::CoreType::TENSIX], 140);
    EXPECT_EQ(per_core_type.at(tt::CoreType::DRAM), 8);
    EXPECT_EQ(per_core_type[tt::CoreType::WORKER], 0);
    EXPECT_THROW(per_core_type.at(tt::CoreType::UNSPECIFIED), std::out_of_range);
}
