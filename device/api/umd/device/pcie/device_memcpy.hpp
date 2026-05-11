// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>

namespace tt::umd {

/**
 * Streaming memcpy for writes targeting device memory mapped through a TLB window.
 *
 * Standard memcpy (glibc) can emit overlapping stores to the same address, which causes
 * double writes when the destination is device memory. This routine guarantees each
 * destination address is written exactly once.
 *
 * Uses explicit 8-byte and 4-byte volatile stores for the aligned middle, with byte
 * stores for any sub-word leading/trailing portion. The Blackhole PCIe controller
 * supports sub-DWORD writes natively, so no read-modify-write is required for the
 * unaligned bytes.
 */
void streaming_memcpy_to_device(volatile void* dest, const void* src, std::size_t size);

/**
 * Streaming memcpy for reads from device memory mapped through a TLB window.
 *
 * Uses explicit 8-byte and 4-byte volatile loads for the aligned middle, with byte
 * loads for any sub-word leading/trailing portion. Each device address is read at
 * most once.
 */
void streaming_memcpy_from_device(void* dest, const volatile void* src, std::size_t size);

}  // namespace tt::umd
