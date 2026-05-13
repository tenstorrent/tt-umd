// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
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
 */
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size);

/**
 * Deadline-aware overload of memcpy_to_device.
 *
 * The deadline is checked after every individual TLB-touching instruction:
 * each byte-wide volatile store, each 4-byte volatile store, each SSE
 * 128-bit store, and each AVX2 256-bit store. SIMD loads from host memory
 * in this function do not get a check (they don't touch the TLB). On
 * overrun a tt::umd::error::DeviceTimeoutError is thrown.
 *
 * The check cannot preempt a stalled MMIO instruction; it fires the moment
 * the previous transaction commits. Worst-case wall time is therefore
 *   `deadline - entry + (PCIe completion timeout for one in-flight store)`.
 *
 * Trade-off: each check_deadline call acts as a compiler barrier. This is
 * intentional (it pins the volatile-conceptual SIMD ordering) but it
 * reduces per-call bulk throughput compared to the non-deadline overload.
 */
void memcpy_to_device(
    volatile void* dest, const void* src, std::size_t size, std::chrono::steady_clock::time_point deadline);

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
 */
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size);

/**
 * Deadline-aware overload of memcpy_from_device.
 *
 * Mirror of memcpy_to_device but the per-instruction check is placed after
 * every TLB-touching *load*: byte-wide volatile load, 4-byte volatile load,
 * SSE 128-bit load, AVX2 256-bit load. SIMD stores to host memory do not
 * get a check. The check between loads serializes the non-posted PCIe
 * reads that would otherwise pipeline — a deliberate throughput-for-
 * responsiveness trade-off.
 */
void memcpy_from_device(
    void* dest, const volatile void* src, std::size_t size, std::chrono::steady_clock::time_point deadline);

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
