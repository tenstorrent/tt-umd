// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "simulation/word_access.hpp"

using namespace tt::umd;

namespace {

// 16 bytes of 0xAA, written through a transport that, like the simulator, takes only whole aligned words.
std::vector<uint8_t> write(uint64_t addr, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> memory(16, 0xAA);
    auto read_words = [&](uint64_t a, void* d, uint32_t n) {
        EXPECT_TRUE(a % 4 == 0 && n % 4 == 0);
        std::memcpy(d, memory.data() + a, n);
    };
    auto write_words = [&](uint64_t a, const void* d, uint32_t n) {
        EXPECT_TRUE(a % 4 == 0 && n % 4 == 0);
        std::memcpy(memory.data() + a, d, n);
    };
    write_bytes_as_words(addr, data.data(), static_cast<uint32_t>(data.size()), read_words, write_words);
    return memory;
}

}  // namespace

TEST(WordAccess, WritesOneByteInsideAWord) {
    const std::vector<uint8_t> expected = {
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x5C, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    EXPECT_EQ(write(5, {0x5C}), expected);
}

TEST(WordAccess, WritesAnUnalignedRangeAcrossWords) {
    const std::vector<uint8_t> expected = {0xAA, 0xAA, 0xAA, 1, 2, 3, 4, 5, 6, 7, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    EXPECT_EQ(write(3, {1, 2, 3, 4, 5, 6, 7}), expected);
}
