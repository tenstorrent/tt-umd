/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "umd/device/cluster.h"

namespace test_utils {

template <typename T>
static void size_buffer_to_capacity(std::vector<T> &data_buf, std::size_t size_in_bytes) {
    std::size_t target_size = 0;
    if (size_in_bytes > 0) {
        target_size = ((size_in_bytes - 1) / sizeof(T)) + 1;
    }
    data_buf.resize(target_size);
}

static void read_data_from_device(tt_device& device, std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {
    size_buffer_to_capacity(vec, size);
    device.read_from_device(vec.data(), core, addr, size, tlb_to_use);
}

}
