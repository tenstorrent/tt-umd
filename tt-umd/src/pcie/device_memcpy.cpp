// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/device_memcpy.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <type_traits>

#include "pcie/memcpy_timing_recorder.hpp"
#include "tt-umd-utils/op_timeout_guard.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/mmio_timeout_config.hpp"

#if defined(__x86_64__) && defined(__AVX2__)
#include <immintrin.h>
#endif

namespace tt::umd {

namespace {

// Bulk transfers move this many bytes per timed op, so a stalled op is caught without timing every load.
constexpr std::size_t BLOCK_SIZE = 256;
constexpr std::size_t CHUNK32 = 32;
constexpr std::size_t CHUNK16 = 16;
constexpr std::size_t DWORD = 4;

// Per-op timing summaries are a debugging aid, dumped to stderr when this variable is set.
bool timing_summary_enabled() {
    static const bool enabled = std::getenv("TT_UMD_MEMCPY_TIMING") != nullptr;
    return enabled;
}

// Runs each device-touching op against the per-op MMIO budget, and records it for the timing summary.
class TimedOps {
public:
    TimedOps(
        const char* fn_name, const char* operation, std::size_t total_size, const std::function<bool()>& on_timeout) :
        recorder_(fn_name, total_size, timing_summary_enabled()),
        guard_(
            MmioTimeoutConfig::get_op_timeout(),
            on_timeout,
            [operation, total_size](
                std::chrono::nanoseconds delta, std::uint32_t op_bytes, std::size_t bytes_remaining) {
                UMD_THROW(
                    error::DeviceTimeoutError,
                    operation,
                    op_bytes,
                    delta,
                    MmioTimeoutConfig::get_op_timeout(),
                    bytes_remaining,
                    total_size);
            }) {}

    // bytes_remaining counts the bytes still to transfer when the op starts, the op included.
    template <typename Op>
    void run(std::uint32_t op_bytes, std::size_t bytes_remaining, Op op) {
        const auto start = std::chrono::steady_clock::now();
        op();
        recorder_.record(start, op_bytes);
        guard_.record_and_check(start, op_bytes, bytes_remaining);
    }

private:
    using OnOverrun = std::function<void(std::chrono::nanoseconds, std::uint32_t, std::size_t)>;

    MemcpyTimingRecorder recorder_;
    OpTimeoutGuard<OnOverrun> guard_;
};

// Single accesses of each width. Device-side pointers are volatile so that every access is issued exactly
// once, at exactly its own width; host-side pointers may be unaligned, so they go through memcpy.
void copy_byte(volatile std::uint8_t* dst, const std::uint8_t* src) { *dst = *src; }

void copy_byte(std::uint8_t* dst, const volatile std::uint8_t* src) { *dst = *src; }

void copy_dword(volatile std::uint8_t* dst, const std::uint8_t* src) {
    std::uint32_t value;
    std::memcpy(&value, src, sizeof(value));
    *reinterpret_cast<volatile std::uint32_t*>(dst) = value;
}

void copy_dword(std::uint8_t* dst, const volatile std::uint8_t* src) {
    const std::uint32_t value = *reinterpret_cast<const volatile std::uint32_t*>(src);
    std::memcpy(dst, &value, sizeof(value));
}

#if defined(__x86_64__) && defined(__AVX2__)

// Unaligned 128/256-bit loads and stores: neither side needs more than DWORD alignment.
template <typename Dst, typename Src>
void copy16(Dst* dst, Src* src) {
    const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(const_cast<const std::uint8_t*>(src)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(const_cast<std::uint8_t*>(dst)), v);
}

template <typename Dst, typename Src>
void copy32(Dst* dst, Src* src) {
    const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(const_cast<const std::uint8_t*>(src)));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(const_cast<std::uint8_t*>(dst)), v);
}

// All loads are issued before any store, so reads from the device pipeline instead of serializing.
template <typename Dst, typename Src>
void copy_block(Dst* dst, Src* src) {
    const auto* s = reinterpret_cast<const __m256i*>(const_cast<const std::uint8_t*>(src));
    auto* d = reinterpret_cast<__m256i*>(const_cast<std::uint8_t*>(dst));
    const __m256i v0 = _mm256_loadu_si256(s + 0);
    const __m256i v1 = _mm256_loadu_si256(s + 1);
    const __m256i v2 = _mm256_loadu_si256(s + 2);
    const __m256i v3 = _mm256_loadu_si256(s + 3);
    const __m256i v4 = _mm256_loadu_si256(s + 4);
    const __m256i v5 = _mm256_loadu_si256(s + 5);
    const __m256i v6 = _mm256_loadu_si256(s + 6);
    const __m256i v7 = _mm256_loadu_si256(s + 7);
    _mm256_storeu_si256(d + 0, v0);
    _mm256_storeu_si256(d + 1, v1);
    _mm256_storeu_si256(d + 2, v2);
    _mm256_storeu_si256(d + 3, v3);
    _mm256_storeu_si256(d + 4, v4);
    _mm256_storeu_si256(d + 5, v5);
    _mm256_storeu_si256(d + 6, v6);
    _mm256_storeu_si256(d + 7, v7);
}

// Wide accesses tolerate any DWORD-aligned device address.
constexpr std::size_t WIDE_ALIGNMENT = DWORD;

#elif defined(__GNUC__)

// Vector extensions lower to the widest native loads and stores (LDP/STP of Q registers on AArch64).
// They require a 16-byte aligned device address, which the caller establishes first.
using Vec16 = std::uint8_t __attribute__((vector_size(16)));
using Vec32 = std::uint8_t __attribute__((vector_size(32), aligned(16)));

template <typename Vec>
void copy_vec(volatile std::uint8_t* dst, const std::uint8_t* src) {
    Vec v;
    std::memcpy(&v, src, sizeof(v));
#if defined(__clang__)
    // Non-temporal stores keep a large write from evicting the cache for data the host never reads back.
    __builtin_nontemporal_store(v, const_cast<Vec*>(reinterpret_cast<volatile Vec*>(dst)));
#else
    *reinterpret_cast<volatile Vec*>(dst) = v;
#endif
}

template <typename Vec>
void copy_vec(std::uint8_t* dst, const volatile std::uint8_t* src) {
    const Vec v = *reinterpret_cast<const volatile Vec*>(src);
    std::memcpy(dst, &v, sizeof(v));
}

template <typename Dst, typename Src>
void copy16(Dst* dst, Src* src) {
    copy_vec<Vec16>(dst, src);
}

template <typename Dst, typename Src>
void copy32(Dst* dst, Src* src) {
    copy_vec<Vec32>(dst, src);
}

template <typename Dst, typename Src>
void copy_block(Dst* dst, Src* src) {
    // Prefetch the host side ahead of the bulk loop.
    __builtin_prefetch(const_cast<const std::uint8_t*>(src) + 2048);
    for (std::size_t offset = 0; offset < BLOCK_SIZE; offset += CHUNK32) {
        copy32(dst + offset, src + offset);
    }
}

constexpr std::size_t WIDE_ALIGNMENT = CHUNK16;

#else

// No wide accesses are available: everything above a DWORD is a run of DWORD accesses.
template <typename Dst, typename Src>
void copy_dwords(Dst* dst, Src* src, std::size_t size) {
    for (std::size_t offset = 0; offset < size; offset += DWORD) {
        copy_dword(dst + offset, src + offset);
    }
}

template <typename Dst, typename Src>
void copy16(Dst* dst, Src* src) {
    copy_dwords(dst, src, CHUNK16);
}

template <typename Dst, typename Src>
void copy32(Dst* dst, Src* src) {
    copy_dwords(dst, src, CHUNK32);
}

template <typename Dst, typename Src>
void copy_block(Dst* dst, Src* src) {
    copy_dwords(dst, src, BLOCK_SIZE);
}

constexpr std::size_t WIDE_ALIGNMENT = DWORD;

#endif

// Copies size bytes between host and device memory. Exactly one of Dst and Src is volatile: that side is
// the device, whose address is aligned with narrow accesses before any wide access touches it.
template <typename Dst, typename Src>
void device_copy(Dst* dst, Src* src, std::size_t size, TimedOps& ops) {
    constexpr bool device_is_dst = std::is_volatile_v<Dst>;
    auto device_address = [&]() {
        return reinterpret_cast<std::uintptr_t>(
            device_is_dst ? static_cast<const volatile void*>(dst) : static_cast<const volatile void*>(src));
    };
    auto step = [&](std::size_t op_size, auto copy) {
        ops.run(static_cast<std::uint32_t>(op_size), size, [&] { copy(dst, src); });
        dst += op_size;
        src += op_size;
        size -= op_size;
    };
    auto byte_op = [](Dst* d, Src* s) { copy_byte(d, s); };
    auto dword_op = [](Dst* d, Src* s) { copy_dword(d, s); };

    // Bytes up to a DWORD boundary, then DWORDs up to the boundary wide accesses need.
    while (size > 0 && device_address() % DWORD != 0) {
        step(1, byte_op);
    }
    while (size >= CHUNK16 && device_address() % WIDE_ALIGNMENT != 0) {
        step(DWORD, dword_op);
    }
    while (size >= BLOCK_SIZE) {
        step(BLOCK_SIZE, [](Dst* d, Src* s) { copy_block(d, s); });
    }
    while (size >= CHUNK32) {
        step(CHUNK32, [](Dst* d, Src* s) { copy32(d, s); });
    }
    if (size >= CHUNK16) {
        step(CHUNK16, [](Dst* d, Src* s) { copy16(d, s); });
    }
    while (size >= DWORD) {
        step(DWORD, dword_op);
    }
    while (size > 0) {
        step(1, byte_op);
    }
}

template <typename T>
void write_scalar(volatile void* dest, T value, const char* fn_name, const std::function<bool()>& on_timeout) {
    TimedOps ops(fn_name, "store", sizeof(T), on_timeout);
    ops.run(sizeof(T), sizeof(T), [&] { *static_cast<volatile T*>(dest) = value; });
}

template <typename T>
T read_scalar(const volatile void* src, const char* fn_name, const std::function<bool()>& on_timeout) {
    TimedOps ops(fn_name, "load", sizeof(T), on_timeout);
    T value{};
    ops.run(sizeof(T), sizeof(T), [&] { value = *static_cast<const volatile T*>(src); });
    return value;
}

}  // namespace

void memcpy_to_device(volatile void* dest, const void* src, std::size_t size, const std::function<bool()>& on_timeout) {
    TimedOps ops("memcpy_to_device", "store", size, on_timeout);
    device_copy(static_cast<volatile std::uint8_t*>(dest), static_cast<const std::uint8_t*>(src), size, ops);
}

void memcpy_from_device(
    void* dest, const volatile void* src, std::size_t size, const std::function<bool()>& on_timeout) {
    TimedOps ops("memcpy_from_device", "load", size, on_timeout);
    device_copy(static_cast<std::uint8_t*>(dest), static_cast<const volatile std::uint8_t*>(src), size, ops);
}

void write16_to_device(volatile void* dest, std::uint16_t value, const std::function<bool()>& on_timeout) {
    write_scalar(dest, value, "write16_to_device", on_timeout);
}

void write32_to_device(volatile void* dest, std::uint32_t value, const std::function<bool()>& on_timeout) {
    write_scalar(dest, value, "write32_to_device", on_timeout);
}

void write64_to_device(volatile void* dest, std::uint64_t value, const std::function<bool()>& on_timeout) {
    write_scalar(dest, value, "write64_to_device", on_timeout);
}

std::uint16_t read16_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    return read_scalar<std::uint16_t>(src, "read16_from_device", on_timeout);
}

std::uint32_t read32_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    return read_scalar<std::uint32_t>(src, "read32_from_device", on_timeout);
}

std::uint64_t read64_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    return read_scalar<std::uint64_t>(src, "read64_from_device", on_timeout);
}

}  // namespace tt::umd
