// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/device_memcpy.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "pcie/memcpy_timing_recorder.hpp"
#include "tt-umd-utils/op_timeout_guard.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/mmio_timeout_config.hpp"

#if defined(__x86_64__) && defined(__AVX2__)
#include <immintrin.h>
#define UMD_DEVICE_MEMCPY_AVX2 1
#endif

namespace tt::umd {

namespace {

// Setting this variable makes every transfer print a one-line timing summary to stderr.
constexpr const char* MEMCPY_TIMING_ENV = "TT_UMD_MEMCPY_TIMING";

bool is_timing_enabled() {
    static const bool enabled = std::getenv(MEMCPY_TIMING_ENV) != nullptr;
    return enabled;
}

constexpr std::size_t BLOCK_SIZE = 256;

// Times every TLB-touching op of one transfer against the per-op budget, records it for the timing
// summary, and aborts the transfer on a confirmed overrun.
class TransferTimer {
public:
    TransferTimer(const char* operation, std::size_t total_size, const std::function<bool()>& on_timeout) :
        operation_(operation),
        total_size_(total_size),
        budget_(MmioTimeoutConfig::get_op_timeout()),
        recorder_(operation, total_size, is_timing_enabled()),
        guard_(
            budget_, on_timeout, [this](std::chrono::nanoseconds delta, std::uint32_t op_bytes, std::size_t remaining) {
                UMD_THROW(error::DeviceTimeoutError, operation_, op_bytes, delta, budget_, remaining, total_size_);
            }) {}

    // Runs one op of op_bytes bytes, with bytes_remaining still to transfer including this op.
    template <typename Op>
    void run(std::uint32_t op_bytes, std::size_t bytes_remaining, Op&& op) {
        const auto start = std::chrono::steady_clock::now();
        op();
        recorder_.record(start, op_bytes);
        guard_.record_and_check(start, op_bytes, bytes_remaining);
    }

private:
    using OverrunAction = std::function<void(std::chrono::nanoseconds, std::uint32_t, std::size_t)>;

    const char* operation_;
    std::size_t total_size_;
    std::chrono::milliseconds budget_;
    MemcpyTimingRecorder recorder_;
    OpTimeoutGuard<OverrunAction> guard_;
};

template <typename T>
T load_unaligned(const std::uint8_t* src) {
    T value;
    std::memcpy(&value, src, sizeof(T));
    return value;
}

template <typename T>
void store_unaligned(std::uint8_t* dest, T value) {
    std::memcpy(dest, &value, sizeof(T));
}

#ifdef UMD_DEVICE_MEMCPY_AVX2
// The device pointer is volatile so the compiler never merges, splits or drops accesses to it; the
// vector intrinsics take plain pointers but emit exactly one access each.
inline __m256i* as_m256(volatile std::uint8_t* p) { return reinterpret_cast<__m256i*>(const_cast<std::uint8_t*>(p)); }

inline const __m256i* as_m256(const volatile std::uint8_t* p) {
    return reinterpret_cast<const __m256i*>(const_cast<const std::uint8_t*>(p));
}

inline __m128i* as_m128(volatile std::uint8_t* p) { return reinterpret_cast<__m128i*>(const_cast<std::uint8_t*>(p)); }

inline const __m128i* as_m128(const volatile std::uint8_t* p) {
    return reinterpret_cast<const __m128i*>(const_cast<const std::uint8_t*>(p));
}
#endif

}  // namespace

void memcpy_to_device(volatile void* dest, const void* src, std::size_t size, const std::function<bool()>& on_timeout) {
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    const auto* s = static_cast<const std::uint8_t*>(src);
    TransferTimer timer("memcpy_to_device", size, on_timeout);

    // Byte stores until the device address is DWORD aligned.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) & 3) != 0) {
        timer.run(1, size, [&] { *d = *s; });
        d++;
        s++;
        size--;
    }

#ifdef UMD_DEVICE_MEMCPY_AVX2
    while (size >= BLOCK_SIZE) {
        timer.run(BLOCK_SIZE, size, [&] {
            for (std::size_t offset = 0; offset < BLOCK_SIZE; offset += sizeof(__m256i)) {
                _mm256_storeu_si256(
                    as_m256(d + offset), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + offset)));
            }
        });
        d += BLOCK_SIZE;
        s += BLOCK_SIZE;
        size -= BLOCK_SIZE;
    }

    while (size >= sizeof(__m256i)) {
        timer.run(sizeof(__m256i), size, [&] {
            _mm256_storeu_si256(as_m256(d), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s)));
        });
        d += sizeof(__m256i);
        s += sizeof(__m256i);
        size -= sizeof(__m256i);
    }

    if (size >= sizeof(__m128i)) {
        timer.run(sizeof(__m128i), size, [&] {
            _mm_storeu_si128(as_m128(d), _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)));
        });
        d += sizeof(__m128i);
        s += sizeof(__m128i);
        size -= sizeof(__m128i);
    }
#endif

    while (size >= sizeof(std::uint32_t)) {
        timer.run(sizeof(std::uint32_t), size, [&] {
            *reinterpret_cast<volatile std::uint32_t*>(d) = load_unaligned<std::uint32_t>(s);
        });
        d += sizeof(std::uint32_t);
        s += sizeof(std::uint32_t);
        size -= sizeof(std::uint32_t);
    }

    while (size > 0) {
        timer.run(1, size, [&] { *d = *s; });
        d++;
        s++;
        size--;
    }
}

void memcpy_from_device(
    void* dest, const volatile void* src, std::size_t size, const std::function<bool()>& on_timeout) {
    auto* d = static_cast<std::uint8_t*>(dest);
    const auto* s = static_cast<const volatile std::uint8_t*>(src);
    TransferTimer timer("memcpy_from_device", size, on_timeout);

    // Byte loads until the device address is DWORD aligned.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) & 3) != 0) {
        timer.run(1, size, [&] { *d = *s; });
        d++;
        s++;
        size--;
    }

#ifdef UMD_DEVICE_MEMCPY_AVX2
    while (size >= BLOCK_SIZE) {
        timer.run(BLOCK_SIZE, size, [&] {
            for (std::size_t offset = 0; offset < BLOCK_SIZE; offset += sizeof(__m256i)) {
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + offset), _mm256_loadu_si256(as_m256(s + offset)));
            }
        });
        d += BLOCK_SIZE;
        s += BLOCK_SIZE;
        size -= BLOCK_SIZE;
    }

    while (size >= sizeof(__m256i)) {
        timer.run(sizeof(__m256i), size, [&] {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), _mm256_loadu_si256(as_m256(s)));
        });
        d += sizeof(__m256i);
        s += sizeof(__m256i);
        size -= sizeof(__m256i);
    }

    if (size >= sizeof(__m128i)) {
        timer.run(sizeof(__m128i), size, [&] {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d), _mm_loadu_si128(as_m128(s)));
        });
        d += sizeof(__m128i);
        s += sizeof(__m128i);
        size -= sizeof(__m128i);
    }
#endif

    while (size >= sizeof(std::uint32_t)) {
        timer.run(sizeof(std::uint32_t), size, [&] {
            store_unaligned<std::uint32_t>(d, *reinterpret_cast<const volatile std::uint32_t*>(s));
        });
        d += sizeof(std::uint32_t);
        s += sizeof(std::uint32_t);
        size -= sizeof(std::uint32_t);
    }

    while (size > 0) {
        timer.run(1, size, [&] { *d = *s; });
        d++;
        s++;
        size--;
    }
}

namespace {

template <typename T>
void write_to_device(const char* operation, volatile void* dest, T value, const std::function<bool()>& on_timeout) {
    TransferTimer timer(operation, sizeof(T), on_timeout);
    timer.run(sizeof(T), sizeof(T), [&] { *static_cast<volatile T*>(dest) = value; });
}

template <typename T>
T read_from_device(const char* operation, const volatile void* src, const std::function<bool()>& on_timeout) {
    TransferTimer timer(operation, sizeof(T), on_timeout);
    T value;
    timer.run(sizeof(T), sizeof(T), [&] { value = *static_cast<const volatile T*>(src); });
    return value;
}

}  // namespace

void write16_to_device(volatile void* dest, std::uint16_t value, const std::function<bool()>& on_timeout) {
    write_to_device("write16_to_device", dest, value, on_timeout);
}

void write32_to_device(volatile void* dest, std::uint32_t value, const std::function<bool()>& on_timeout) {
    write_to_device("write32_to_device", dest, value, on_timeout);
}

void write64_to_device(volatile void* dest, std::uint64_t value, const std::function<bool()>& on_timeout) {
    write_to_device("write64_to_device", dest, value, on_timeout);
}

std::uint16_t read16_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    return read_from_device<std::uint16_t>("read16_from_device", src, on_timeout);
}

std::uint32_t read32_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    return read_from_device<std::uint32_t>("read32_from_device", src, on_timeout);
}

std::uint64_t read64_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    return read_from_device<std::uint64_t>("read64_from_device", src, on_timeout);
}

}  // namespace tt::umd
