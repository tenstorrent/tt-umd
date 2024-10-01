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
    virtual void write_u8(uint32_t address, uint8_t value) = 0;
    virtual void write_u32(uint32_t address, uint32_t value) = 0;
    virtual void write(uint32_t address, uint8_t* arr_ptr, uint32_t size) = 0;
    virtual void write(uint32_t address, uint32_t* arr_ptr, uint32_t size) = 0;

    virtual read_u8(uint32_t address) = 0;
    virtual uint32_t read_u32(uint32_t address) = 0;
    virtual void read(uint32_t address, uint8_t* arr_ptr, uint32_t size) = 0;
    virtual void read(uint32_t address, uint32_t* arr_ptr, uint32_t size) = 0;
};

}