// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>

// Backports of standard library facilities newer than the C++17 this library is built with. Each one
// carries the signature of the facility it names, so a use site needs no change when the file goes away.

namespace tt::umd {

// std::to_underlying from C++23, for the places that need an enum as a number: bit operations, message ids,
// logging, serialisation. Not for indexing, that is what EnumArray is for. Non-enums fail to substitute
// the return type, so there is no overload to call.
template <typename Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum value) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value);
}

}  // namespace tt::umd
