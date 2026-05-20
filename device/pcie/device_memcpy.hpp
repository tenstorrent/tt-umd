// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace tt::umd {

/**
 * Optional callback invoked when an MMIO op exceeds the per-op budget.
 *
 * Return `true` to abort the memcpy (throws tt::umd::error::DeviceTimeoutError).
 * Return `false` to treat the slow op as a false positive — the memcpy
 * continues with the next op getting a fresh budget.
 *
 * Typical use is a NOC hang check: probe the device cheaply and return
 * "is this op stalled because the device is actually hung?".
 *
 * The implementation must not recurse into the same memcpy / I/O path that
 * triggered it. A thread-local re-entrancy guard inside memcpy short-circuits
 * one level of recursion (the inner timeout will simply throw without
 * re-invoking the callback), but composability and lock-ordering across
 * the I/O stack are the caller's responsibility.
 */
using MemcpyTimeoutFn = std::function<bool()>;

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
 * Per-op budget: TLB-touching stores must complete within a hard-coded budget
 * (default 100 ms, overridable at process start via TT_UMD_MMIO_OP_TIMEOUT_MS).
 * The check is applied once per 256-byte block in the bulk AVX2 phase, and once
 * per op in the 32 / 16 / 4-byte and byte-wide tail phases.
 * On overrun:
 *   - if no on_timeout is provided, throws tt::umd::error::DeviceTimeoutError;
 *   - if on_timeout is provided and returns true, throws DeviceTimeoutError;
 *   - if on_timeout is provided and returns false, the memcpy continues with the
 *     next op getting a fresh budget.
 */
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size, const MemcpyTimeoutFn& on_timeout = {});

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
 * Per-op budget semantics match memcpy_to_device. Inserting a check between loads
 * serializes the non-posted PCIe reads that would otherwise pipeline — a deliberate
 * throughput-for-responsiveness trade-off.
 */
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size, const MemcpyTimeoutFn& on_timeout = {});

/**
 * Single-DWORD/word scalar transfers to/from TLB-mapped device memory. Centralizing these
 * here (alongside the bulk memcpy routines) keeps every TLB-touching store/load behind a
 * single set of primitives that future improvements (per-op timeouts, instrumentation,
 * etc.) can wrap uniformly.
 */
void write16_to_device(volatile void* dest, std::uint16_t value);
void write32_to_device(volatile void* dest, std::uint32_t value);
std::uint16_t read16_from_device(const volatile void* src);
std::uint32_t read32_from_device(const volatile void* src);

}  // namespace tt::umd
