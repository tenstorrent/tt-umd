// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

class DeviceProtocol;
class TlbWindow;

// NoC requests to MMIO registers must be exactly one register wide; a wider request returns or stores a
// single register. These issue a multi-register transfer as one 4-byte access per register, so it never
// reaches the device as one wide request. The address and size must be 4-byte aligned.
void read_registers_individually(
    DeviceProtocol& protocol, void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id);
void write_registers_individually(
    DeviceProtocol& protocol, const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id);

// Same split over a window's block path, for the simulation windows driven directly instead of through a
// protocol. Temporary: goes away when simulation moves under the DeviceProtocol umbrella.
void read_registers_individually(TlbWindow& window, uint64_t offset, void* data, size_t size);
void write_registers_individually(TlbWindow& window, uint64_t offset, const void* data, size_t size);

}  // namespace tt::umd
