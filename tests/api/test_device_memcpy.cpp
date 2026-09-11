// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Off-device correctness tests for memcpy_to_device / memcpy_from_device.
//
// These tests exercise all internal phases using a heap buffer as a
// stand-in for device MMIO (volatile void*). No hardware is required.
//
// Phase coverage:
//   Phase 0 — byte-by-byte alignment of the destination to 4 bytes
//   Phase 1 — bulk 256-byte blocks (AVX2 on x86; STNP Q pairs on AArch64)
//   Phase 2 — remaining 32-byte chunks
//   Phase 3 — remaining 16-byte chunk
//   Phase 4 — remaining 4-byte chunks
//   Phase 5 — trailing 1–3 bytes.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "pcie/device_memcpy.hpp"

using namespace tt::umd;

namespace {

// Pad on each side to catch over-reads / over-writes.
constexpr size_t kGuardBytes = 64;
constexpr uint8_t kGuardFill = 0xAB;
constexpr uint8_t kSrcFill = 0x42;

// Verify that fn(dst, src, size) writes exactly the right bytes to dst and
// leaves everything outside [dst_offset, dst_offset+size) untouched.
// fn receives raw (void*, const void*, size_t) and is responsible for any
// volatile casts needed by the underlying memcpy_to/from_device call.
template <typename Fn>
void check_memcpy(const char* label, Fn fn, size_t size, size_t dst_offset, size_t src_offset) {
    std::vector<uint8_t> src_buf(src_offset + size + kGuardBytes, 0);
    std::vector<uint8_t> dst_buf(dst_offset + size + kGuardBytes, kGuardFill);

    // Recognisable pattern that wraps to catch byte-swaps.
    for (size_t i = 0; i < size; ++i) {
        src_buf[src_offset + i] = static_cast<uint8_t>(kSrcFill + i);
    }

    fn(dst_buf.data() + dst_offset, src_buf.data() + src_offset, size);

    for (size_t i = 0; i < size; ++i) {
        ASSERT_EQ(dst_buf[dst_offset + i], src_buf[src_offset + i])
            << label << " mismatch at byte " << i << " (size=" << size << " dst_off=" << dst_offset
            << " src_off=" << src_offset << ")";
    }
    for (size_t i = 0; i < dst_offset; ++i) {
        ASSERT_EQ(dst_buf[i], kGuardFill) << label << " underrun at " << i;
    }
    for (size_t i = dst_offset + size; i < dst_buf.size(); ++i) {
        ASSERT_EQ(dst_buf[i], kGuardFill) << label << " overrun at " << i;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Parameterised correctness: {size, dst_misalignment, src_misalignment}
// ---------------------------------------------------------------------------

struct MemcpyParam {
    size_t size;
    size_t dst_off;
    size_t src_off;
};

class DeviceMemcpyToDevice : public ::testing::TestWithParam<MemcpyParam> {};

class DeviceMemcpyFromDevice : public ::testing::TestWithParam<MemcpyParam> {};

TEST_P(DeviceMemcpyToDevice, Correctness) {
    const auto& p = GetParam();
    check_memcpy(
        "to_device",
        [](void* d, const void* s, size_t n) { memcpy_to_device(static_cast<volatile void*>(d), s, n); },
        p.size,
        p.dst_off,
        p.src_off);
}

TEST_P(DeviceMemcpyFromDevice, Correctness) {
    const auto& p = GetParam();
    check_memcpy(
        "from_device",
        [](void* d, const void* s, size_t n) { memcpy_from_device(d, static_cast<const volatile void*>(s), n); },
        p.size,
        p.dst_off,
        p.src_off);
}

// clang-format off
static const MemcpyParam kCases[] = {
    // --- Phase 5 only (< 4 bytes, no 4B store) ---
    {1, 0, 0}, {2, 0, 0}, {3, 0, 0},
    // --- Phase 4 tail (4-byte stores, no SIMD) ---
    {4, 0, 0}, {5, 0, 0}, {7, 0, 0}, {8, 0, 0}, {12, 0, 0}, {15, 0, 0},
    // --- Phase 3 (16-byte chunk) ---
    {16, 0, 0}, {17, 0, 0}, {19, 0, 0}, {31, 0, 0},
    // --- Phase 2 (32-byte chunks) ---
    {32, 0, 0}, {33, 0, 0}, {47, 0, 0}, {63, 0, 0}, {64, 0, 0},
    // --- Phase 1 (bulk 256-byte blocks) ---
    {256, 0, 0}, {257, 0, 0}, {512, 0, 0}, {768, 0, 0},
    {1024, 0, 0}, {4096, 0, 0}, {16384, 0, 0},
    // --- Unaligned destination: triggers Phase 0 byte-alignment loop ---
    {64,   1, 0}, {64,   2, 0}, {64,   3, 0},
    {256,  1, 0}, {256,  2, 0}, {256,  3, 0},
    {1024, 1, 0}, {1024, 3, 0},
    // --- 4-byte-aligned but 16-byte-misaligned: triggers GNUC 16-byte alignment loop ---
    {256,  4, 0}, {256,  8, 0}, {256, 12, 0},
    {1024, 4, 0}, {1024, 8, 0}, {1024, 12, 0},
    // --- Unaligned source ---
    {64,   0, 1}, {64,   0, 2}, {64,   0, 3},
    {256,  0, 1}, {1024, 0, 3},
    // --- 4-byte-aligned but 16-byte-misaligned source ---
    {256,  0, 4}, {256,  0, 8}, {256,  0, 12},
    {1024, 0, 4}, {1024, 0, 12},
    // --- Both unaligned ---
    {64,   1, 3}, {256,  3, 1}, {1024, 2, 3},
    // --- Both 16-byte-misaligned ---
    {256,  4, 8}, {1024, 8, 4},
};
// clang-format on

INSTANTIATE_TEST_SUITE_P(
    AllPhasesAndAlignments,
    DeviceMemcpyToDevice,
    ::testing::ValuesIn(kCases),
    [](const ::testing::TestParamInfo<MemcpyParam>& info) {
        return "sz" + std::to_string(info.param.size) + "_d" + std::to_string(info.param.dst_off) + "_s" +
               std::to_string(info.param.src_off);
    });

INSTANTIATE_TEST_SUITE_P(
    AllPhasesAndAlignments,
    DeviceMemcpyFromDevice,
    ::testing::ValuesIn(kCases),
    [](const ::testing::TestParamInfo<MemcpyParam>& info) {
        return "sz" + std::to_string(info.param.size) + "_d" + std::to_string(info.param.dst_off) + "_s" +
               std::to_string(info.param.src_off);
    });
