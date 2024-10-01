/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

namespace tt::umd {

class TLBWindow {

public:
    virtual ~TLBWindow() = default;

    TLBWindow(void* ptr, size_t size) : ptr(ptr), size(size) {}

    size_t size() const { return size; }
    void* raw_ptr() { return ptr; }

    void write8(uint64_t address, uint8_t value) {
        write_block(address, &value, sizeof(value));
    }

    void write16(uint64_t address, uint16_t value) {
        write_block(address, &value, sizeof(value));
    }

    void write32(uint64_t address, uint32_t value) {
        write_block(address, &value, sizeof(value));
    }

    void write64(uint64_t address, uint64_t value) {
        write_block(address, &value, sizeof(value));
    }

    uint8_t read8(uint64_t address) {
        uint8_t value;
        read_block(address, &value, sizeof(value));
        return value;
    }

    uint16_t read16(uint64_t address) {
        uint16_t value;
        read_block(address, &value, sizeof(value));
        return value;
    }

    uint32_t read32(uint64_t address) {
        uint32_t value;
        read_block(address, &value, sizeof(value));
        return value;
    }

    uint64_t read64(uint64_t address) {
        uint64_t value;
        read_block(address, &value, sizeof(value));
        return value;
    }

    virtual void write_block(uint64_t address, const void* buffer, size_t size);
    virtual void read_block(uint64_t address, void* buffer, size_t size);
private:
    void* ptr;
    size_t size;
};

}