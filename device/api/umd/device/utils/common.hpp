/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "umd/device/tt_xy_pair.h"

static inline std::vector<tt_xy_pair> flatten_vector(const std::vector<std::vector<tt_xy_pair>>& vector_of_vectors) {
    std::vector<tt_xy_pair> flat_vector;
    for (const auto& single_vector : vector_of_vectors) {
        flat_vector.insert(flat_vector.end(), single_vector.begin(), single_vector.end());
    }
    return flat_vector;
}

static inline std::string to_lower(const std::string& str) {
    std::string res = str;
    std::transform(res.begin(), res.end(), res.begin(), ::tolower);
    return res;
}
