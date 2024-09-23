/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

namespace tt_driver_atomics {

static inline __attribute__((always_inline)) void sfence() {
    // Full memory barrier (full system). ARM does not have a Store-Any barrier.
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile ("DMB SY" : : : "memory");
}

static inline __attribute__((always_inline)) void lfence() {
    // Load-Any barrier (full system)
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile ("DMB LD" : : : "memory");
}

static inline __attribute__((always_inline)) void mfence() {
    // Full memory barrier (full system).
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile ("DMB SY" : : : "memory");
}

} // namespace tt_driver_atomics