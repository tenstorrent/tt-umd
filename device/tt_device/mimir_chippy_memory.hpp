// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// INTERNAL, chippy-aware header (C++20). Do not include from public UMD headers.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "address_translation.h"  // chippy
#include "transport_interface.h"  // chippy

#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"

namespace tt::umd {

class SocDescriptor;

/**
 * Chippy Memory windows for a one- or two-Mimir package reached in chiplet-local AXI.
 *
 * Used by both emu_axi and JTAG2AXI so aligned SRAM/DRAM/config accesses take the same
 * bulk_read_bytes / bulk_write_bytes path. Unaligned or partial transfers fall back to
 * TransportInterface::read/write with a 4-byte minimum word size.
 */
class MimirChippyMemoryMap {
public:
    using Transport = chippy::transport::TransportInterface;

    MimirChippyMemoryMap(
        const SocDescriptor& soc_descriptor,
        std::vector<std::shared_ptr<Transport>> chiplet_transports,
        std::string device_name);

    void read(uint64_t flat_address, void* dst, std::size_t size) const;
    void write(uint64_t flat_address, const void* src, std::size_t size);

private:
    using ChippyMemory = chippy::address_translation::Memory<std::uint32_t>;

    struct MemoryRegion {
        uint64_t flat_base = 0;
        uint64_t local_base = 0;
        std::shared_ptr<Transport> transport;
        std::unique_ptr<ChippyMemory> memory;

        bool contains(uint64_t address, std::size_t size) const;
        std::size_t offset(uint64_t address) const;
        uint64_t local_address(uint64_t address) const;
        void read(uint64_t address, void* dst, std::size_t size) const;
        void write(uint64_t address, const void* src, std::size_t size);
    };

    MemoryRegion make_region(
        uint64_t flat_base, uint64_t local_base, std::size_t size, const std::shared_ptr<Transport>& transport);
    MemoryRegion* find_memory(uint64_t address, std::size_t size) const;

    std::string device_name_;
    // Memory stores a reference to this flag; it must outlive every ChippyMemory.
    bool use_global_addressing_ = false;
    mutable std::vector<MemoryRegion> memory_regions_;
};

}  // namespace tt::umd
