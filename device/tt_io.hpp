// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <cstdint>
#include <stdexcept>

class tt_SiliconDevice;

namespace tt {

/**
 * @brief Provides write access to a SoC core via a statically-mapped TLB.
 *
 * TLB refers to the aperture within the device BAR that is mapped to a NOC
 * endpoint (i.e. an (X, Y) location + address) within the chip.
 *
 * It is the caller's responsibility to manage the lifetime of Writer objects.
 */
class Writer
{
    friend class ::tt_SiliconDevice;

public:
    /**
     * @brief Write to a SoC core.
     * 
     * @param address must be aligned to the size of T
     * @param value 
     */
    template <class T>
    void write(uint32_t address, T value)
    {
        auto dst = reinterpret_cast<uintptr_t>(base) + address;

        if (address >= tlb_size) {
            throw std::runtime_error("Address out of bounds for TLB");
        }

        if (alignof(T) > 1 && (dst & (alignof(T) - 1))) {
            throw std::runtime_error("Unaligned write");
        }

        *reinterpret_cast<volatile T*>(dst) = value;
    }

private:
    /**
     * @brief tt_SiliconDriver interface to construct a new Writer object.
     * 
     * @param base pointer to the base address of a mapped TLB.
     * @param tlb_size size of the mapped TLB.
     */
    Writer(void *base, size_t tlb_size)
        : base(base)
        , tlb_size(tlb_size)
    {
        assert(base);
        assert(tlb_size > 0);
    }

    void *base{ nullptr };
    size_t tlb_size{ 0 };
};


} // namespace tt
