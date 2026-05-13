// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

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
 * Per-op budget: every individual TLB-touching store (byte, 4-byte, SSE 128-bit, AVX2
 * 256-bit) must complete within a hard-coded budget (default 30 ms) — overridable at
 * process start via the env var `TT_UMD_MMIO_OP_TIMEOUT_MS`. On overrun the function
 * throws tt::umd::error::DeviceTimeoutError. Total memcpy wall time is NOT bounded —
 * only single-op stalls are caught. The check fires once the previous transaction
 * commits; worst-case detection latency is therefore
 *   `op_timeout + PCIe completion timeout for the stalled transaction`.
 */
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size);

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
 * Handles arbitrary alignment and size.
 *
 * Per-op budget: same semantics as memcpy_to_device, applied to TLB loads. Inserting
 * a check between loads serializes the non-posted PCIe reads that would otherwise
 * pipeline — a deliberate throughput-for-responsiveness trade-off.
 */
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size);

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
