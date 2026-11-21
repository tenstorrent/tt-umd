// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::umd {

/**
 * Turns a flat address into a transport access.
 *
 * The resolver produces the flat address. This is the only thing that knows whether that address
 * is already the address on the wire or still has to be folded back onto a chiplet-local bus.
 */
class FlatAddressTransport {
public:
    virtual ~FlatAddressTransport() = default;

    virtual void read(uint64_t flat_address, void* dst, std::size_t size) const = 0;
    virtual void write(uint64_t flat_address, const void* src, std::size_t size) = 0;
};

}  // namespace tt::umd
