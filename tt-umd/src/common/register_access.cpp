// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "register_access.hpp"

#include <stdexcept>

#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "utils.hpp"

namespace tt::umd {

void read_registers_individually(
    DeviceProtocol& protocol, void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    validate_register_access(addr, size);
    for (size_t offset = 0; offset < size; offset += sizeof(uint32_t)) {
        protocol.read_data(static_cast<uint8_t*>(dst) + offset, core, addr + offset, sizeof(uint32_t), noc_id);
    }
}

void write_registers_individually(
    DeviceProtocol& protocol, const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    validate_register_access(addr, size);
    for (size_t offset = 0; offset < size; offset += sizeof(uint32_t)) {
        protocol.write_data(static_cast<const uint8_t*>(src) + offset, core, addr + offset, sizeof(uint32_t), noc_id);
    }
}

// Each per-register block access checks only its own register, so check the whole range first: an
// out-of-window request must throw before any register is touched, not after a partial update.
static void validate_window_range(const TlbWindow& window, uint64_t offset, size_t size) {
    if (offset + size > window.get_size()) {
        throw std::out_of_range("Out of bounds access");
    }
}

void read_registers_individually(TlbWindow& window, uint64_t offset, void* data, size_t size) {
    validate_register_access(offset, size);
    validate_window_range(window, offset, size);
    for (size_t data_offset = 0; data_offset < size; data_offset += sizeof(uint32_t)) {
        window.read_block(offset + data_offset, static_cast<uint8_t*>(data) + data_offset, sizeof(uint32_t));
    }
}

void write_registers_individually(TlbWindow& window, uint64_t offset, const void* data, size_t size) {
    validate_register_access(offset, size);
    validate_window_range(window, offset, size);
    for (size_t data_offset = 0; data_offset < size; data_offset += sizeof(uint32_t)) {
        window.write_block(offset + data_offset, static_cast<const uint8_t*>(data) + data_offset, sizeof(uint32_t));
    }
}

}  // namespace tt::umd
