/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

// Internal header. read_flat_address stays protected: it is the hook TTDevice uses after the
// resolver has already produced an address. Tests include this to observe that address directly.

#include <cstddef>
#include <cstdint>

#include "tt-umd/tt_device/grendel_tt_device.hpp"

namespace tt::umd {

struct GrendelTTDeviceTestAccess {
    static void read_flat(GrendelTTDevice& device, uint64_t address, void* dst, size_t size) {
        device.read_flat_address(address, dst, size);
    }
};

}  // namespace tt::umd
