// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device_memcpy.hpp"

#include <chrono>
#include <cstdlib>
#include <string>

namespace tt::umd {

namespace {

// Hard-coded default per-op budget; overridable at process start via the env var
// TT_UMD_MMIO_OP_TIMEOUT_MS. Applied once per 256-byte block in the bulk AVX2
// phase and once per op in the 32 / 16 / 4-byte and byte-wide tail phases. Set to
// 100 ms so post-reset reads (which can legitimately take tens of ms before the
// device settles) don't trip the timeout on the happy path.
constexpr std::chrono::milliseconds kDefaultMmioOpTimeout{100};

}  // namespace

std::chrono::milliseconds mmio_op_timeout() {
    static const std::chrono::milliseconds value = [] {
        const char* env = std::getenv("TT_UMD_MMIO_OP_TIMEOUT_MS");
        if (env == nullptr || *env == '\0') {
            return kDefaultMmioOpTimeout;
        }
        try {
            return std::chrono::milliseconds(std::stoul(env));
        } catch (...) {
            return kDefaultMmioOpTimeout;
        }
    }();
    return value;
}

}  // namespace tt::umd
