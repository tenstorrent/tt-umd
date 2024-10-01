/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

#include "abstract_io.h"

namespace tt::umd {

// This abstracts all readers and writers.
// A user can hold this object to bypass the need to go through regular interfaces and 
class RemoteCoreIO : public AbstractIO {

public:
    void write_u8(uint32_t address, uint8_t value) override;
    void write_u32(uint32_t address, uint32_t value) override;
    void write(uint32_t address, uint8_t* arr_ptr, uint32_t size) override;
    void write(uint32_t address, uint32_t* arr_ptr, uint32_t size) override;

    uint8_t read_u8(uint32_t address) override;
    uint32_t read_u32(uint32_t address) override;
    void read(uint32_t address, uint8_t* arr_ptr, uint32_t size) override;
    void read(uint32_t address, uint32_t* arr_ptr, uint32_t size) override;
};

}