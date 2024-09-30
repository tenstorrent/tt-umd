/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "chip/soc_descriptor.h"
#include "io/abstract_io.h"

namespace tt::umd {

// This is a layer which should be used by a regular user.
// This hides implementation details for local, remote, versim, and mock cores.
class Core {

   private:
    // constructor is hidden and called only by the chip.
    // Also it is called for a specific type, local, remote, versim, mock core.
    Core(physical_coord core, CoreType type);

   public:
    virtual void deassert_risc_reset();
    virtual void assert_risc_reset();

    // read write functions
    virtual void write_to_device(const void* mem_ptr, uint32_t size_in_bytes, uint64_t addr);
    virtual void write_reg_to_device(const void* mem_ptr, uint32_t size_in_bytes, uint64_t addr);
    virtual void write_to_device(std::vector<uint32_t>& vec, uint64_t addr);
    virtual void write_reg_to_device(std::vector<uint32_t>& vec, uint64_t addr);
    virtual void read_from_device(void* mem_ptr, uint64_t addr, uint32_t size);
    virtual void read_reg_from_device(void* mem_ptr, uint64_t addr, uint32_t size);
    virtual void read_from_device(std::vector<uint32_t>& vec, uint64_t addr, uint32_t size);
    virtual void read_reg_from_device(std::vector<uint32_t>& vec, uint64_t addr, uint32_t size);

    // Returns local_core_io, remote_core_io, local_dram_io, remote_dram_io, based on the core type.
    virtual std::unique_ptr<AbstractIO> get_io(uint64_t base_addr = 0, uint64_t size = 0);

};

}