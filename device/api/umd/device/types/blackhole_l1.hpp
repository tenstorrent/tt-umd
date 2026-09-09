// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd::blackhole {

// L1 address map constants needed by UMD.
// Tied to the blackhole L1 memory layout used by the RISC firmware.
// Tensix L1 barrier region, 32 bytes.
inline constexpr uint32_t L1_BARRIER_BASE = 0x16dfc0;
// ERISC L1 barrier region, 32 bytes.
inline constexpr uint32_t ERISC_BARRIER_BASE = 0x11fe0;
// Start of the NCRISC firmware region in tensix L1.
inline constexpr uint32_t NCRISC_FIRMWARE_BASE = 0x5000;
// First tensix L1 address that is not reserved by the firmware.
inline constexpr uint32_t DATA_BUFFER_SPACE_BASE = 0x37000;

}  // namespace tt::umd::blackhole
