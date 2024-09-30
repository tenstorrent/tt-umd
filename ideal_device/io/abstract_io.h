/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

namespace tt::umd {

// This abstracts all readers and writers.
// A user can hold this object to bypass the need to go through regular interfaces and 
class AbstractIO {
    public:
    void write_u8(uint32_t address, uint8_t value);
    void write_u32(uint32_t address, uint32_t value);
    void write(uint32_t address, uint8_t* arr_ptr, uint32_t size);
    void write(uint32_t address, uint32_t* arr_ptr, uint32_t size);

    uint8_t read_u8(uint32_t address);
    uint32_t read_u32(uint32_t address);
    void read(uint32_t address, uint8_t* arr_ptr, uint32_t size);
    void read(uint32_t address, uint32_t* arr_ptr, uint32_t size);
};

}