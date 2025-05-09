/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/sysmem_buffer.h"

namespace tt::umd {

SysmemBuffer::SysmemBuffer(void* buffer_va, uint32_t buffer_size, uint64_t device_io_addr) :
    buffer_va(buffer_va), buffer_size(buffer_size), device_io_addr(device_io_addr) {}

}  // namespace tt::umd
