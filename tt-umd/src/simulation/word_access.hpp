// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace tt::umd {

// Byte-granular access over a transport that moves only whole, aligned 32-bit words, such as the RTL
// simulator's GLOBAL_READ/GLOBAL_WRITE. read_words(addr, data, size) and write_words(addr, data, size)
// are called only with word-aligned addr and size.
//
// A write with a partial first or last word reads that word, merges the new bytes and writes it back
// whole, so the bytes around the range keep their values. That read-merge-write isn't atomic: the
// caller serializes host access (RtlSimulationTTDevice holds its device lock), but a device-side
// writer to the same word in between would be overwritten.

inline constexpr uint64_t WORD_BYTES = sizeof(uint32_t);

constexpr uint64_t align_down_to_word(uint64_t value) { return value & ~(WORD_BYTES - 1); }

constexpr uint64_t align_up_to_word(uint64_t value) { return align_down_to_word(value + WORD_BYTES - 1); }

/** Read @p size bytes at @p addr: reads the whole words that cover the range and keeps its bytes. */
template <typename ReadWords>
void read_bytes_as_words(uint64_t addr, void *data, uint32_t size, ReadWords read_words) {
    const uint64_t start = align_down_to_word(addr);
    const uint64_t end = align_up_to_word(addr + size);
    if (start == addr && end == addr + size) {
        read_words(addr, data, size);
        return;
    }
    std::vector<uint8_t> words(end - start);
    read_words(start, words.data(), static_cast<uint32_t>(words.size()));
    std::memcpy(data, words.data() + (addr - start), size);
}

/** Write @p size bytes at @p addr, reading and merging a partial first or last word. */
template <typename ReadWords, typename WriteWords>
void write_bytes_as_words(
    uint64_t addr, const void *data, uint32_t size, ReadWords read_words, WriteWords write_words) {
    const uint64_t start = align_down_to_word(addr);
    const uint64_t end = align_up_to_word(addr + size);
    if (start == addr && end == addr + size) {
        write_words(addr, data, size);
        return;
    }
    std::vector<uint8_t> words(end - start);
    if (start != addr) {
        read_words(start, words.data(), static_cast<uint32_t>(WORD_BYTES));
    }
    // The last word, unless the range ends on a word boundary or it is the first word, already read.
    if (end != addr + size && (end - start > WORD_BYTES || start == addr)) {
        read_words(end - WORD_BYTES, words.data() + words.size() - WORD_BYTES, static_cast<uint32_t>(WORD_BYTES));
    }
    std::memcpy(words.data() + (addr - start), data, size);
    write_words(start, words.data(), static_cast<uint32_t>(words.size()));
}

}  // namespace tt::umd
