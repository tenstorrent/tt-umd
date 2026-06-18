// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace tt::umd {

/**
 * memcpy for writes targeting device memory mapped through a TLB window.
 *
 * Standard memcpy (glibc) can emit overlapping stores to the same address, which causes
 * double writes when the destination is device memory. This routine issues each store
 * explicitly so every destination address is written exactly once.
 *
 * On x86_64: bulk transfers use AVX2 unaligned loads/stores (VMOVDQU 256-bit), with
 * 16-byte (SSE), 4-byte and byte-wide tails.
 *
 * On non-x86 with GCC/Clang (e.g. AArch64): uses vector extensions for
 * 32/16-byte wide stores; Clang gets non-temporal stores (STNP on AArch64)
 * with 2 KB software prefetch.
 *
 * On other architectures: falls back to explicit 4-byte and byte-wide volatile stores.
 *
 * Handles arbitrary alignment and size — leading/trailing bytes smaller than a DWORD are
 * written as individual byte-wide PCIe transactions (the Blackhole PCIe controller supports
 * sub-DWORD writes natively, so no read-modify-write is required).
 *
 * Each TLB-touching op is bounded by a per-op timeout; on overrun the optional on_timeout callback
 * decides whether to abort with tt::umd::error::DeviceTimeoutError (see the on_timeout doc above).
 */
void memcpy_to_device(
    volatile void* dest, const void* src, std::size_t size, const std::function<bool()>& on_timeout = {});

/**
 * memcpy for reads from device memory mapped through a TLB window.
 *
 * On x86_64: bulk transfers use AVX2 unaligned loads/stores (VMOVDQU 256-bit), with
 * 16-byte (SSE), 4-byte and byte-wide tails.
 *
 * On non-x86 with GCC/Clang (e.g. AArch64): uses vector extensions for
 * 32/16-byte wide loads/stores (LDP/STP on AArch64) with 2 KB prefetch.
 *
 * On other architectures: falls back to explicit 4-byte and byte-wide volatile loads.
 *
 * Per-op timeout semantics match memcpy_to_device; the check falls between bulk blocks (not between
 * individual loads), so the non-posted PCIe reads within a block still pipeline.
 */
void memcpy_from_device(
    void* dest, const volatile void* src, std::size_t size, const std::function<bool()>& on_timeout = {});

/**
 * Single-DWORD/word scalar transfers to/from TLB-mapped device memory. Each carries the same
 * optional per-op budget as the bulk memcpy routines: on overrun the behavior follows on_timeout
 * (no callback throws DeviceTimeoutError; see the on_timeout doc above). Centralizing them here keeps
 * every TLB-touching store/load behind a single set of timed primitives.
 */
void write16_to_device(volatile void* dest, std::uint16_t value, const std::function<bool()>& on_timeout = {});
void write32_to_device(volatile void* dest, std::uint32_t value, const std::function<bool()>& on_timeout = {});
std::uint16_t read16_from_device(const volatile void* src, const std::function<bool()>& on_timeout = {});
std::uint32_t read32_from_device(const volatile void* src, const std::function<bool()>& on_timeout = {});

}  // namespace tt::umd
