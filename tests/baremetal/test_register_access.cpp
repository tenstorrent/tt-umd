// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "utils.hpp"

using namespace tt::umd;

namespace {

std::vector<std::pair<uint64_t, size_t>> accesses(uint64_t addr, size_t size) {
    std::vector<std::pair<uint64_t, size_t>> result;
    for_each_register_access(addr, size, [&](uint64_t reg_addr, size_t offset) { result.emplace_back(reg_addr, offset); });
    return result;
}

}  // namespace

TEST(RegisterAccess, SplitsTransferIntoOneAccessPerRegister) {
    const std::vector<std::pair<uint64_t, size_t>> expected = {{0xFFB20100, 0}, {0xFFB20104, 4}, {0xFFB20108, 8}};
    EXPECT_EQ(accesses(0xFFB20100, 12), expected);
}

TEST(RegisterAccess, ThrowsOnUnalignedAddress) { EXPECT_ANY_THROW(accesses(0xFFB20102, 4)); }

TEST(RegisterAccess, ThrowsOnUnalignedSize) { EXPECT_ANY_THROW(accesses(0xFFB20100, 6)); }
