// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace tt::umd {

// std::to_underlying from C++23, for the places that need an enum as a number: bit operations, message ids,
// logging, serialisation. Not for indexing, that is what EnumArray is for. Non-enums fail to substitute
// the return type, so there is no overload to call.
template <typename Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum value) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value);
}

namespace detail {

// C++17 stand-in for std::is_scoped_enum (C++23). An unscoped enum converts implicitly to its underlying
// type, a scoped one does not. The bool parameter keeps underlying_type_t from being named for non-enums.
template <typename E, bool = std::is_enum_v<E>>
struct is_scoped_enum : std::false_type {};

template <typename E>
struct is_scoped_enum<E, true> : std::bool_constant<!std::is_convertible_v<E, std::underlying_type_t<E>>> {};

}  // namespace detail

// True when E can index an EnumArray: E is an enum class and has a COUNT enumerator.
//
// That is all a C++17 trait can check. The rest of the contract is on the enum's author: enumerators
// start at 0, are contiguous, and COUNT is the last one. An enumerator declared after COUNT is outside
// the array; indexing with it through operator[] is undefined behaviour, exactly like std::array, and
// through at() it throws.
template <typename E, typename = void>
struct is_enum_array_index : std::false_type {};

template <typename E>
struct is_enum_array_index<E, std::void_t<decltype(E::COUNT)>> : detail::is_scoped_enum<E> {};

template <typename E>
inline constexpr bool is_enum_array_index_v = is_enum_array_index<E>::value;

// A std::array<T, N> whose only index is a scoped enum, with N taken from Enum::COUNT.
//
// Replaces the `std::array<T, N>` + `static_cast<size_t>(key)` pattern and the dense `std::map<Enum, T>`:
// the integer index is not reachable, so an enum cannot be confused with an index or with another enum,
// and the size follows the enum, so adding an enumerator resizes every array and breaks every
// element-wise construction site that was not updated.
//
// Composes std::array rather than inheriting from it, so std::array's operator[](size_t) is not part of
// the interface. Storage is value-initialised, so a default-constructed EnumArray<int, E> holds zeros.
template <typename T, typename Enum>
class EnumArray {
    static_assert(is_enum_array_index_v<Enum>, "EnumArray index must be an enum class with a COUNT enumerator.");
    static_assert(to_underlying(Enum::COUNT) > 0, "EnumArray index enum has nothing before COUNT.");

public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type SIZE = static_cast<size_type>(Enum::COUNT);

    using reference = T&;
    using const_reference = const T&;
    using iterator = typename std::array<T, SIZE>::iterator;
    using const_iterator = typename std::array<T, SIZE>::const_iterator;

    constexpr EnumArray() = default;

    // Element-wise construction, one argument per enumerator, in declaration order. Exactly SIZE arguments
    // are required, so a mismatch is a compile error rather than silently zero-filled tail. Convertibility
    // is part of the constraint so that this never competes with the copy and move constructors.
    template <
        typename... Args,
        std::enable_if_t<sizeof...(Args) == SIZE && std::conjunction_v<std::is_convertible<Args, T>...>, int> = 0>
    constexpr EnumArray(Args&&... args) : data_{{std::forward<Args>(args)...}} {}

    constexpr reference operator[](Enum key) noexcept { return data_[to_index(key)]; }

    constexpr const_reference operator[](Enum key) const noexcept { return data_[to_index(key)]; }

    // Bounds-checked access. Only reachable with an enumerator that breaks the contract (declared after
    // COUNT, or given an explicit value), so it is a way to catch such enums rather than a hot-path API.
    constexpr reference at(Enum key) { return data_[checked_index(key)]; }

    constexpr const_reference at(Enum key) const { return data_[checked_index(key)]; }

    static constexpr size_type size() noexcept { return SIZE; }

    // Every enumerator in declaration order, for loops that need the key as well as the value:
    //   for (const Enum key : array.keys()) { use(key, array[key]); }
    static constexpr std::array<Enum, SIZE> keys() noexcept { return make_keys(std::make_index_sequence<SIZE>{}); }

    constexpr iterator begin() noexcept { return data_.begin(); }

    constexpr const_iterator begin() const noexcept { return data_.begin(); }

    constexpr iterator end() noexcept { return data_.end(); }

    constexpr const_iterator end() const noexcept { return data_.end(); }

    // Not std::array::fill, which is constexpr only from C++20.
    constexpr void fill(const T& value) {
        for (T& element : data_) {
            element = value;
        }
    }

    // Not std::array::operator==, which is constexpr only from C++20.
    friend constexpr bool operator==(const EnumArray& lhs, const EnumArray& rhs) {
        for (size_type index = 0; index < SIZE; ++index) {
            if (!(lhs.data_[index] == rhs.data_[index])) {
                return false;
            }
        }
        return true;
    }

    friend constexpr bool operator!=(const EnumArray& lhs, const EnumArray& rhs) { return !(lhs == rhs); }

private:
    template <std::size_t... Indices>
    static constexpr std::array<Enum, SIZE> make_keys(std::index_sequence<Indices...>) noexcept {
        return {{static_cast<Enum>(Indices)...}};
    }

    // A negative enumerator wraps to a huge index here, which at() then rejects.
    static constexpr size_type to_index(Enum key) noexcept { return static_cast<size_type>(to_underlying(key)); }

    static constexpr size_type checked_index(Enum key) {
        const size_type index = to_index(key);
        if (index >= SIZE) {
            throw std::out_of_range(
                "EnumArray: enumerator with value " + std::to_string(to_underlying(key)) + " is not below COUNT (" +
                std::to_string(SIZE) + ").");
        }
        return index;
    }

    std::array<T, SIZE> data_{};
};

}  // namespace tt::umd
