/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "chip_connection.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {

class SysmemManager;

class IPCIeSpecific {
public:
    virtual ~IPCIeSpecific() = default;
    virtual SysmemManager* get_sysmem_manager() = 0;
    virtual TLBManager* get_tlb_manager() = 0;

    virtual int get_host_channel_size(std::uint32_t channel) = 0;

    virtual void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) = 0;
    virtual void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) = 0;

    virtual void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) = 0;
    virtual void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) = 0;

    virtual std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable() = 0;
    virtual int get_numa_node() = 0;
};

}  // namespace tt::umd
