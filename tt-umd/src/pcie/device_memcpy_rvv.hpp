// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::umd::rvv {

// RVV 1.0 block copies for the riscv64 path in device_memcpy.cpp. This header is only reachable
// when the configure probe in CMakeLists.txt defines TT_UMD_RVV_MEMCPY, which also adds
// device_memcpy_rvv.cpp to the build; see that probe for why support is detected rather than
// inferred from a compiler version.

// True when the host implements V and TT_UMD_DISABLE_RVV_MEMCPY is unset.
bool enabled();

// Copy `bytes`, a multiple of 8, to or from an 8-byte-aligned device address using 64-bit element
// accesses. The host-side pointer has no alignment requirement.
void block_to_device(volatile std::uint8_t* dest, const std::uint8_t* src, std::size_t bytes);
void block_from_device(std::uint8_t* dest, const volatile std::uint8_t* src, std::size_t bytes);

}  // namespace tt::umd::rvv
