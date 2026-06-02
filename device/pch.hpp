// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Precompiled-header contents for the tt-umd library: the heavy, stable, widely-included
// external/system headers that forward declarations cannot remove. Parsed once instead of
// once per (unity) translation unit.
//
// This is a real header file (not an inline list in target_precompile_headers) so it can be
// language-scoped to C++ via $<COMPILE_LANGUAGE:CXX> in CMakeLists.txt: a file path has no
// angle brackets, so it avoids the genex-vs-"<...>" parsing bug that leaks a stray '>' into
// a C precompiled header (which would then fail to compile <fmt>/<vector> as C).
#pragma once

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
