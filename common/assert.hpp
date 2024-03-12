/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <filesystem>
#include <fmt/format.h>


#define UMD_THROW(...)                                                                             \
    do {                                                                                           \
        std::filesystem::path p(__FILE__);                                                         \
        throw std::runtime_error(                                                                  \
            fmt::format("[{}:{}] {}", p.filename().string(), __LINE__, fmt::format(__VA_ARGS__))); \
    } while (0)

