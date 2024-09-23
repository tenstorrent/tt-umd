/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <immintrin.h>

namespace tt_driver_atomics {

// Store-Any barrier.
static inline __attribute__((always_inline)) void sfence() {
    _mm_sfence();
}
// Load-Any barrier.
static inline __attribute__((always_inline)) void lfence() {
    _mm_lfence();
}
// Any-Any barrier.
static inline __attribute__((always_inline)) void mfence() {
    _mm_mfence();
}

} // namespace tt_driver_atomics