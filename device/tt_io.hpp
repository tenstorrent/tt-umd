// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <cstdint>
#include <stdexcept>

class tt_SiliconDevice;

namespace tt {

class Writer
{
    friend class ::tt_SiliconDevice;

public:
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
     * @brief Provides write access to a SoC core via a statically-mapped TLB.
     * 
     * @param base 
     * @param range 
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