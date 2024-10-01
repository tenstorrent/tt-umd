/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

#include "abstract_io.h"
#include "tlb_manager.h"
#include "tlb_window.h"

namespace tt::umd {

// This abstracts all readers and writers.
// A user can hold this object to bypass the need to go through regular interfaces and 
class LocalCoreIO : public AbstractIO {

public:
    LocalCoreIO(tt_xy_pair core, TLBManager* tlb_manager) : core(core), tlb_manager(tlb_manager) {
        tlb_window = tlb_manager->get_tlb_window(core);
    }

    void write_u8(uint32_t address, uint8_t value) override {
        if (address < tlb_window.size()) {
            reg_tlb_window = tlb_manager->get_uc_tlb_window();
            reg_tlb_window.write8(address, value);
            tlb_manager->release_tlb_window(reg_tlb_window);
        } else {
            tlb_window.write8(address, value);
        }
    }

    void write_u32(uint32_t address, uint32_t value) override;
    void write(uint32_t address, uint8_t* arr_ptr, uint32_t size) override;
    void write(uint32_t address, uint32_t* arr_ptr, uint32_t size) override;

    uint8_t read_u8(uint32_t address) override;
    uint32_t read_u32(uint32_t address) override;
    void read(uint32_t address, uint8_t* arr_ptr, uint32_t size) override;
    void read(uint32_t address, uint32_t* arr_ptr, uint32_t size) override;
private:
    // Used to get the TLB window.
    // Not sure whether we should hold directly TLB manager inside LocalCoreIO, or should TLB manager be hidden through LocalChip.
    TLBManager* tlb_manager;

    // Used for actually writing to the core.
    // This is supposed to be WC tlb window.
    TLBWindow tlb_window;

    // Reg TLB window, used for read/write to registers, outside of standard L1 (for example core reset).
    // This is UC tlb window.
    TLBWindow reg_tlb_window;
};

}