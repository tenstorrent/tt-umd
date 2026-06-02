// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Precompiled-header contents for the UMD test executables. Propagated to every test
// target through the test_common INTERFACE library. Holds GoogleTest plus the heavy,
// stable external/system headers that essentially every test translation unit pulls in.
//
// A real header file (rather than an inline list in target_precompile_headers) so it can
// be language-scoped via a file path in $<COMPILE_LANGUAGE:CXX> without the genex-vs-"<...>"
// parsing bug that leaks a stray '>' into a C precompiled header.
#pragma once

#include <fmt/format.h>
#include <gtest/gtest.h>

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
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
