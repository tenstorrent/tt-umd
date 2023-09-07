#ifndef __ARM_ARCH
#include <immintrin.h>
#endif
namespace tt_driver_atomics {
inline __attribute__((always_inline)) void sfence() {
#ifdef __ARM_ARCH
    // Full memory barrier (full system). ARM does not have a Store-Any barrier.
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile ("DMB SY" : : : "memory");
#else
    _mm_sfence();
#endif
}
inline __attribute__((always_inline)) void lfence() {
#ifdef __ARM_ARCH
    // Load-Any barrier (full system)
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile ("DMB LD" : : : "memory");
#else
    _mm_lfence();
#endif
}
inline __attribute__((always_inline)) void mfence() {
#ifdef __ARM_ARCH
    // Full memory barrier (full system).
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile ("DMB SY" : : : "memory");
#else
    _mm_mfence();
#endif
}
}