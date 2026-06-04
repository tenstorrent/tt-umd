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
 * Contract: the callback must issue any device I/O it performs through a path
 * that does NOT re-enter a timed memcpy and does NOT re-take a lock already held
 * by the stalled op. There is no re-entrancy guard inside memcpy — a callback
 * that reads back through a timed path (with an on_timeout of its own) would
 * recurse, and one that re-takes the caller's I/O lock would deadlock. A cheap
 * BAR-based probe (e.g. HangDetector::is_pcie_hung) satisfies this; routing the
 * probe through the locked, TLB-mapped block path (e.g. is_noc_hung via
 * PcieProtocol::read_from_device) does not.
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
 * On other architectures: falls back to explicit 4-byte and byte-wide volatile loads.
 *
 * Per-op budget semantics match memcpy_to_device. The check is placed once per 256-byte
 * AVX2 block (and once per op in the tails), so the 8 non-posted PCIe reads within a block
 * still pipeline; the timeout boundary falls between blocks rather than between loads.
 */
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size, const MemcpyTimeoutFn& on_timeout = {});

/**
 * Single-DWORD/word scalar transfers to/from TLB-mapped device memory. Each carries the same
 * optional per-op budget as the bulk memcpy routines: on overrun the behavior follows on_timeout
 * (no callback throws DeviceTimeoutError; see MemcpyTimeoutFn). Centralizing them here keeps every
 * TLB-touching store/load behind a single set of timed primitives.
 */
void write16_to_device(volatile void* dest, std::uint16_t value, const MemcpyTimeoutFn& on_timeout = {});
void write32_to_device(volatile void* dest, std::uint32_t value, const MemcpyTimeoutFn& on_timeout = {});
std::uint16_t read16_from_device(const volatile void* src, const MemcpyTimeoutFn& on_timeout = {});
std::uint32_t read32_from_device(const volatile void* src, const MemcpyTimeoutFn& on_timeout = {});

}  // namespace tt::umd
