// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/jtag_communication.hpp"

namespace tt::umd {

void JtagCommunication::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    // Empty implementation
}

void JtagCommunication::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    // Empty implementation
}

void JtagCommunication::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr) {
    // Empty implementation
}

void JtagCommunication::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr) {
    // Empty implementation
}

void JtagCommunication::write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) {
    // Empty implementation
}

void JtagCommunication::write_regs(uint32_t byte_addr, uint32_t word_len, const void* data) {
    // Empty implementation
}

void JtagCommunication::read_regs(uint32_t byte_addr, uint32_t word_len, void* data) {
    // Empty implementation
}

void JtagCommunication::wait_for_non_mmio_flush() {
    // Empty implementation
}

bool JtagCommunication::is_remote() {
    // Empty implementation
    return false;
}

}  // namespace tt::umd
