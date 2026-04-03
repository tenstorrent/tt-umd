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
 * On x86_64: uses non-temporal (streaming) stores via AVX2/SSE, bypassing CPU cache
 * for better throughput than memcpy on write-combining memory.
 *
 * On other architectures: falls back to explicit single-byte/4-byte stores (no double writes).
 *
 * Handles arbitrary alignment and size — leading/trailing bytes smaller than a DWORD are
 * written as individual byte-wide PCIe transactions (the Blackhole PCIe controller supports
 * sub-DWORD writes natively, so no read-modify-write is required).
 */
void streaming_memcpy_to_device(volatile void* dest, const void* src, std::size_t size);

/**
 * Streaming memcpy for reads from device memory mapped through a TLB window.
 *
 * On x86_64: uses non-temporal (streaming) loads via AVX/AVX2 and SSE4.1 (MOVNTDQA),
 * avoiding CPU cache pollution from device data that won't be reused.
 *
 * On other architectures: falls back to explicit single-byte/4-byte loads.
 *
 * Handles arbitrary alignment and size.
 */
void streaming_memcpy_from_device(void* dest, const volatile void* src, std::size_t size);

}  // namespace tt::umd
