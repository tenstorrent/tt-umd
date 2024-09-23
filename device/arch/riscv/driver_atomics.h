/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

namespace tt_driver_atomics {

static inline __attribute__((always_inline)) void sfence() {
    asm volatile ("fence ow, ow" : : : "memory");
}

static inline __attribute__((always_inline)) void lfence() {
    asm volatile ("fence ir, ir" : : : "memory");
}

static inline __attribute__((always_inline)) void mfence() {
    asm volatile ("fence iorw, iorw" : : : "memory");
}

} // namespace tt_driver_atomics