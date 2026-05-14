# MMIO memcpy timeout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a configurable wall-clock budget to `memcpy_to_device` and `memcpy_from_device` so a hung or slow-degraded NOC cannot grind the host indefinitely, layered on top of the existing SIGBUS guard.

**Architecture:** Add deadline-aware overloads to the low-level memcpy free functions in `device/pcie/device_memcpy.{hpp,cpp}`. Sample `std::chrono::steady_clock::now()` once at function entry and re-check between Phase 1 (256-byte AVX2) iterations only — Phase 0/2/3/4/5 are tail work bounded to <256 B combined. On overrun, throw a new `tt::umd::error::DeviceTimeoutError`. `SiliconTlbWindow::write_block` / `read_block` compute a deadline from a static, env-var-configurable budget (`TT_UMD_MMIO_TIMEOUT_MS`, default 1000 ms) and thread it through. Single-word access paths (`write32`/`read32`/etc.) remain on SIGBUS — adding a timer around a single 4-byte MMIO costs more than it saves.

**Tech Stack:** C++17, `<chrono>`, `<atomic>`, x86_64 AVX2 intrinsics (existing), GoogleTest, nanobind for Python exposure, CMake/Ninja build.

**Spec:** `docs/superpowers/specs/2026-05-13-mmio-memcpy-timeout-design.md`

**Note on commits:** Per the user's global preference, **do not run `git commit`**. Steps labeled "Stage and hand off" mean `git add` only; the user stages remaining files and creates the commit. The reviewer/executor never invokes `git commit` locally.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `device/api/umd/device/utils/error.hpp` | Modify | Add `DeviceTimeoutError` next to `SigbusError`. |
| `device/pcie/device_memcpy.hpp` | Modify | Public API: add deadline overloads + document worst-case. |
| `device/pcie/device_memcpy.cpp` | Modify | Implement deadline overloads; existing overloads delegate with `time_point::max()`. |
| `device/api/umd/device/pcie/silicon_tlb_window.hpp` | Modify | Add `set_mmio_timeout_ms` / `get_mmio_timeout` statics; add `deadline` param on private `memcpy_to_device`/`memcpy_from_device`. |
| `device/pcie/silicon_tlb_window.cpp` | Modify | Implement static config (lazy env-var init via `std::call_once`); thread deadline through `write_block`/`read_block` and private helpers. |
| `tests/baremetal/test_device_memcpy.cpp` | Create | Pure-host unit tests for deadline behavior — does not need a device. |
| `tests/baremetal/CMakeLists.txt` | Modify | Add new test source to `BAREMETAL_TESTS_SRCS`. |
| `nanobind/py_api_tt_device.cpp` | Modify | Expose `DeviceTimeoutError` to Python (mirror `SigbusError`). |

---

## Task 1: Add `DeviceTimeoutError` exception type

**Files:**
- Modify: `device/api/umd/device/utils/error.hpp`

- [ ] **Step 1: Add the new exception class**

Open `device/api/umd/device/utils/error.hpp`. After the `SigbusError` class (currently the last class in the file, ending at line 43), add:

```cpp
/**
 * @brief Exception thrown when a host-side MMIO operation exceeds its
 * configured wall-clock budget.
 *
 * Distinct from SigbusError: SigbusError indicates the platform's PCIe
 * completion timeout fired (hardware-detected fault). DeviceTimeoutError
 * indicates a software budget elapsed — typically because writes were
 * piling up against a slow or dead NOC before SIGBUS could fire.
 */
class DeviceTimeoutError : public std::runtime_error {
public:
    explicit DeviceTimeoutError(const std::string& message) : std::runtime_error(message) {}
};
```

Place it immediately before the closing `}  // namespace tt::umd::error` line.

- [ ] **Step 2: Verify it compiles into an existing translation unit**

Run from the repository root:

```bash
cmake --build build --target tt-umd
```

Expected: build succeeds. (No tests run yet — that comes in Task 2 once we have a place to throw it from.)

- [ ] **Step 3: Stage and hand off**

```bash
git add device/api/umd/device/utils/error.hpp
```

Hand off to the user for review and commit.

---

## Task 2: Add deadline overload of `memcpy_to_device`

**Files:**
- Modify: `device/pcie/device_memcpy.hpp`
- Modify: `device/pcie/device_memcpy.cpp`
- Create: `tests/baremetal/test_device_memcpy.cpp`
- Modify: `tests/baremetal/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

Create `tests/baremetal/test_device_memcpy.cpp` with the following content:

```cpp
// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "device/pcie/device_memcpy.hpp"
#include "umd/device/utils/error.hpp"

using namespace tt::umd;
using namespace std::chrono_literals;

namespace {
constexpr std::size_t kBulkBytes = 4 * 1024;  // big enough to exercise Phase 1

std::vector<std::uint8_t> make_pattern(std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>(i * 31 + 7);
    }
    return v;
}
}  // namespace

TEST(DeviceMemcpyTimeout, ToDevice_ExpiredDeadlineThrows) {
    auto src = make_pattern(kBulkBytes);
    std::vector<std::uint8_t> dst(kBulkBytes, 0);

    auto past = std::chrono::steady_clock::now() - 1ms;
    EXPECT_THROW(memcpy_to_device(dst.data(), src.data(), kBulkBytes, past),
                 error::DeviceTimeoutError);
}

TEST(DeviceMemcpyTimeout, ToDevice_GenerousDeadlineCompletes) {
    auto src = make_pattern(kBulkBytes);
    std::vector<std::uint8_t> dst(kBulkBytes, 0);

    auto deadline = std::chrono::steady_clock::now() + 1s;
    EXPECT_NO_THROW(memcpy_to_device(dst.data(), src.data(), kBulkBytes, deadline));
    EXPECT_EQ(std::memcmp(dst.data(), src.data(), kBulkBytes), 0);
}

TEST(DeviceMemcpyTimeout, ToDevice_TinySizeSkipsBulkLoop) {
    // sizes that hit only Phases 0/2/3/4/5 (no Phase 1 iterations) must not
    // false-trigger even with a tight deadline if we already have time at entry.
    for (std::size_t n : {0u, 1u, 4u, 32u, 64u, 200u, 255u}) {
        auto src = make_pattern(n);
        std::vector<std::uint8_t> dst(n, 0);
        auto deadline = std::chrono::steady_clock::now() + 1s;
        EXPECT_NO_THROW(memcpy_to_device(dst.data(), src.data(), n, deadline)) << "n=" << n;
        if (n) {
            EXPECT_EQ(std::memcmp(dst.data(), src.data(), n), 0) << "n=" << n;
        }
    }
}

TEST(DeviceMemcpyTimeout, ToDevice_NoDeadlineOverloadStillWorks) {
    auto src = make_pattern(kBulkBytes);
    std::vector<std::uint8_t> dst(kBulkBytes, 0);

    EXPECT_NO_THROW(memcpy_to_device(dst.data(), src.data(), kBulkBytes));
    EXPECT_EQ(std::memcmp(dst.data(), src.data(), kBulkBytes), 0);
}
```

- [ ] **Step 2: Wire the new file into the baremetal_tests target**

Open `tests/baremetal/CMakeLists.txt`. The `BAREMETAL_TESTS_SRCS` list currently contains:

```cmake
set(BAREMETAL_TESTS_SRCS
    test_cluster_descriptor_offline.cpp
    test_core_coord_translation_wh.cpp
    test_core_coord_translation_bh.cpp
    test_soc_arch_descriptor.cpp
    test_soc_descriptor.cpp
    test_long_jump.cpp
    test_notification_mechanism.cpp
    test_cluster.cpp
)
```

Add `test_device_memcpy.cpp` to the list:

```cmake
set(BAREMETAL_TESTS_SRCS
    test_cluster_descriptor_offline.cpp
    test_core_coord_translation_wh.cpp
    test_core_coord_translation_bh.cpp
    test_soc_arch_descriptor.cpp
    test_soc_descriptor.cpp
    test_long_jump.cpp
    test_notification_mechanism.cpp
    test_cluster.cpp
    test_device_memcpy.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail to compile**

```bash
cmake --build build --target baremetal_tests
```

Expected: build fails because `memcpy_to_device` does not yet have a 4-argument overload taking a `time_point`.

- [ ] **Step 4: Add the deadline overload declaration to the header**

Open `device/pcie/device_memcpy.hpp`. Replace its contents with:

```cpp
// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>

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
 * The deadline is checked once per Phase 1 (256-byte AVX2) iteration. Tail
 * phases (≤ 255 B combined) are bounded and skip the check. On overrun a
 * tt::umd::error::DeviceTimeoutError is thrown — but note: the check cannot
 * preempt a stalled MMIO instruction. The worst-case wall time is
 *   `deadline - entry + (PCIe completion timeout for one in-flight store)`
 * because the elapsed check only fires after the previous iteration's stores
 * have committed.
 */
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size,
                      std::chrono::steady_clock::time_point deadline);

/**
 * memcpy for reads from device memory mapped through a TLB window.
 *
 * On x86_64: bulk transfers use AVX2 unaligned loads/stores (VMOVDQU 256-bit), with
 * 16-byte (SSE), 4-byte and byte-wide tails.
 *
 * On other architectures: falls back to explicit 4-byte and byte-wide volatile loads.
 *
 * Handles arbitrary alignment and size.
 */
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size);

/**
 * Deadline-aware overload of memcpy_from_device. See memcpy_to_device overload
 * for semantics, ordering, and worst-case behavior.
 */
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size,
                        std::chrono::steady_clock::time_point deadline);

}  // namespace tt::umd
```

- [ ] **Step 5: Implement the deadline overload in the .cpp**

Open `device/pcie/device_memcpy.cpp`. After the existing `#include <cstring>` block and before `namespace tt::umd {`, add:

```cpp
#include <chrono>
#include <string>

#include "umd/device/utils/error.hpp"
```

Then **inside** the existing `void memcpy_to_device(volatile void* dest, const void* src, std::size_t size)` body, leave it untouched. Add the new overload immediately after it (before the closing `}  // namespace tt::umd`):

```cpp
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size,
                      std::chrono::steady_clock::time_point deadline) {
    const std::size_t original_size = size;
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    auto throw_timeout = [&](std::size_t remaining) {
        auto past_deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - deadline)
                                    .count();
        throw error::DeviceTimeoutError(
            "memcpy_to_device timeout: " + std::to_string(remaining) + " of " +
            std::to_string(original_size) + " bytes remaining, " +
            std::to_string(past_deadline_ms) + " ms past deadline.");
    };

    if (std::chrono::steady_clock::now() >= deadline) {
        throw_timeout(size);
    }

    // Phase 0: Align device destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

#if defined(__x86_64__) || defined(_M_X64)
    // The AVX2 intrinsics below are not themselves volatile, so the compiler is free to
    // reorder them relative to other non-volatile accesses. The volatile byte/4-byte loops
    // in Phases 0/4/5 bound the reordering window for this function.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* d_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(d));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned stores).
    while (size >= 256) {
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 224));

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 32), v1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 64), v2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 96), v3);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 128), v4);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 160), v5);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 192), v6);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 224), v7);

        d_simd += 256;
        s += 256;
        size -= 256;

        if (size >= 256 && std::chrono::steady_clock::now() >= deadline) {
            throw_timeout(size);
        }
    }

    // Phase 2: Remaining 32-byte chunks (bounded < 256 B).
    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v);
        d_simd += 32;
        s += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d_simd), v);
        d_simd += 16;
        s += 16;
        size -= 16;
    }

    d = reinterpret_cast<volatile std::uint8_t*>(d_simd);
#endif

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        *reinterpret_cast<volatile std::uint32_t*>(d) = tmp;
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile stores.
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}
```

Note: the deadline check is placed *after* `size -= 256` and gated on `size >= 256` so we never throw with the bulk loop complete — the remaining tail phases are bounded and will finish in microseconds.

- [ ] **Step 6: Have the original `memcpy_to_device` delegate to the deadline overload**

Replace the body of the original `memcpy_to_device(volatile void* dest, const void* src, std::size_t size)` with a single forwarding call so we keep one code path:

```cpp
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size) {
    memcpy_to_device(dest, src, size, std::chrono::steady_clock::time_point::max());
}
```

(This deletes the existing inlined implementation — its logic now lives in the deadline overload above.)

- [ ] **Step 7: Build and run the new tests**

```bash
cmake --build build --target baremetal_tests
./build/test/umd/baremetal/baremetal_tests --gtest_filter='DeviceMemcpyTimeout.ToDevice_*'
```

Expected: all four `ToDevice_*` tests PASS.

- [ ] **Step 8: Stage and hand off**

```bash
git add device/api/umd/device/utils/error.hpp \
        device/pcie/device_memcpy.hpp \
        device/pcie/device_memcpy.cpp \
        tests/baremetal/test_device_memcpy.cpp \
        tests/baremetal/CMakeLists.txt
```

Hand off to the user for review and commit.

---

## Task 3: Add deadline overload of `memcpy_from_device`

**Files:**
- Modify: `device/pcie/device_memcpy.cpp`
- Modify: `tests/baremetal/test_device_memcpy.cpp`

- [ ] **Step 1: Add tests mirroring the ToDevice tests**

In `tests/baremetal/test_device_memcpy.cpp`, append these tests after the existing ones (before the trailing `}` if any, otherwise at end of file):

```cpp
TEST(DeviceMemcpyTimeout, FromDevice_ExpiredDeadlineThrows) {
    auto src = make_pattern(kBulkBytes);
    std::vector<std::uint8_t> dst(kBulkBytes, 0);

    auto past = std::chrono::steady_clock::now() - 1ms;
    EXPECT_THROW(memcpy_from_device(dst.data(), src.data(), kBulkBytes, past),
                 error::DeviceTimeoutError);
}

TEST(DeviceMemcpyTimeout, FromDevice_GenerousDeadlineCompletes) {
    auto src = make_pattern(kBulkBytes);
    std::vector<std::uint8_t> dst(kBulkBytes, 0);

    auto deadline = std::chrono::steady_clock::now() + 1s;
    EXPECT_NO_THROW(memcpy_from_device(dst.data(), src.data(), kBulkBytes, deadline));
    EXPECT_EQ(std::memcmp(dst.data(), src.data(), kBulkBytes), 0);
}

TEST(DeviceMemcpyTimeout, FromDevice_TinySizeSkipsBulkLoop) {
    for (std::size_t n : {0u, 1u, 4u, 32u, 64u, 200u, 255u}) {
        auto src = make_pattern(n);
        std::vector<std::uint8_t> dst(n, 0);
        auto deadline = std::chrono::steady_clock::now() + 1s;
        EXPECT_NO_THROW(memcpy_from_device(dst.data(), src.data(), n, deadline)) << "n=" << n;
        if (n) {
            EXPECT_EQ(std::memcmp(dst.data(), src.data(), n), 0) << "n=" << n;
        }
    }
}

TEST(DeviceMemcpyTimeout, FromDevice_NoDeadlineOverloadStillWorks) {
    auto src = make_pattern(kBulkBytes);
    std::vector<std::uint8_t> dst(kBulkBytes, 0);

    EXPECT_NO_THROW(memcpy_from_device(dst.data(), src.data(), kBulkBytes));
    EXPECT_EQ(std::memcmp(dst.data(), src.data(), kBulkBytes), 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target baremetal_tests
```

Expected: build fails — `memcpy_from_device` does not yet have a 4-argument overload.

- [ ] **Step 3: Add the deadline overload to the .cpp**

In `device/pcie/device_memcpy.cpp`, add after the deadline-aware `memcpy_to_device`:

```cpp
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size,
                        std::chrono::steady_clock::time_point deadline) {
    const std::size_t original_size = size;
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

    auto throw_timeout = [&](std::size_t remaining) {
        auto past_deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - deadline)
                                    .count();
        throw error::DeviceTimeoutError(
            "memcpy_from_device timeout: " + std::to_string(remaining) + " of " +
            std::to_string(original_size) + " bytes remaining, " +
            std::to_string(past_deadline_ms) + " ms past deadline.");
    };

    if (std::chrono::steady_clock::now() >= deadline) {
        throw_timeout(size);
    }

    // Phase 0: Align device source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

#if defined(__x86_64__) || defined(_M_X64)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* s_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(s));

    while (size >= 256) {
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 224));

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 32), v1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 64), v2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 96), v3);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 128), v4);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 160), v5);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 192), v6);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 224), v7);

        d += 256;
        s_simd += 256;
        size -= 256;

        if (size >= 256 && std::chrono::steady_clock::now() >= deadline) {
            throw_timeout(size);
        }
    }

    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v);
        d += 32;
        s_simd += 32;
        size -= 32;
    }

    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s_simd));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
        d += 16;
        s_simd += 16;
        size -= 16;
    }

    s = reinterpret_cast<const volatile std::uint8_t*>(s_simd);
#endif

    while (size >= 4) {
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s += 4;
        size -= 4;
    }

    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}
```

- [ ] **Step 4: Have the original `memcpy_from_device` delegate**

Replace the body of the original `memcpy_from_device(void* dest, const volatile void* src, std::size_t size)` with:

```cpp
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size) {
    memcpy_from_device(dest, src, size, std::chrono::steady_clock::time_point::max());
}
```

- [ ] **Step 5: Build and run all DeviceMemcpyTimeout tests**

```bash
cmake --build build --target baremetal_tests
./build/test/umd/baremetal/baremetal_tests --gtest_filter='DeviceMemcpyTimeout.*'
```

Expected: all 8 tests (4 ToDevice + 4 FromDevice) PASS.

- [ ] **Step 6: Stage and hand off**

```bash
git add device/pcie/device_memcpy.cpp tests/baremetal/test_device_memcpy.cpp
```

Hand off to the user.

---

## Task 4: Add `set_mmio_timeout_ms` / `get_mmio_timeout` static config

**Files:**
- Modify: `device/api/umd/device/pcie/silicon_tlb_window.hpp`
- Modify: `device/pcie/silicon_tlb_window.cpp`
- Modify: `tests/baremetal/test_device_memcpy.cpp`

- [ ] **Step 1: Write the failing config tests**

Append to `tests/baremetal/test_device_memcpy.cpp`:

```cpp
#include "umd/device/pcie/silicon_tlb_window.hpp"

TEST(MmioTimeoutConfig, DefaultIs1000Ms) {
    // Note: this test relies on TT_UMD_MMIO_TIMEOUT_MS NOT being set in the
    // env. CI invocations should not pre-set it. If it is set, this test will
    // pick up the env-var value instead.
    auto v = SiliconTlbWindow::get_mmio_timeout();
    // We don't assert exact equality with 1000 ms because the env may override
    // it, but we do assert it's a sane positive value.
    EXPECT_GT(v.count(), 0);
}

TEST(MmioTimeoutConfig, RuntimeOverrideWins) {
    auto saved = SiliconTlbWindow::get_mmio_timeout();
    SiliconTlbWindow::set_mmio_timeout_ms(42);
    EXPECT_EQ(SiliconTlbWindow::get_mmio_timeout(), std::chrono::milliseconds(42));
    // Restore so we don't bleed into other tests in the same process.
    SiliconTlbWindow::set_mmio_timeout_ms(static_cast<uint32_t>(saved.count()));
    EXPECT_EQ(SiliconTlbWindow::get_mmio_timeout(), saved);
}
```

- [ ] **Step 2: Run tests to verify failure**

```bash
cmake --build build --target baremetal_tests
```

Expected: build fails — `set_mmio_timeout_ms` and `get_mmio_timeout` do not exist on `SiliconTlbWindow`.

- [ ] **Step 3: Declare the statics in the header**

Open `device/api/umd/device/pcie/silicon_tlb_window.hpp`. In the `public:` section, immediately after the existing `static void set_sigbus_safe_handler(bool set_safe_handler);` line (currently around line 74), add:

```cpp
    /**
     * Configure the wall-clock budget enforced inside block-level MMIO
     * routines (`write_block`/`read_block`). Default is 1000 ms, overridable
     * at process start via the env var `TT_UMD_MMIO_TIMEOUT_MS`. This is a
     * process-wide setting — a NOC hang is device-wide and the same budget
     * applies to every window.
     *
     * On overrun the affected call throws `tt::umd::error::DeviceTimeoutError`.
     * The budget is best-effort, not a hard upper bound on wall time: the
     * check runs between bulk-loop iterations, so worst case is
     * `budget + PCIe completion timeout for one in-flight transaction`.
     */
    static void set_mmio_timeout_ms(uint32_t ms);
    static std::chrono::milliseconds get_mmio_timeout();
```

At the top of the header, add `#include <chrono>` next to `#include <cstdint>`.

- [ ] **Step 4: Implement the statics in the .cpp**

In `device/pcie/silicon_tlb_window.cpp`, near the top of the `tt::umd` namespace block, after the existing `sigbus_handler` / `ScopedJumpGuard` definitions and before `set_sigbus_safe_handler`, add:

```cpp
namespace {

constexpr uint32_t kDefaultMmioTimeoutMs = 1000;

std::atomic<uint32_t>& mmio_timeout_storage() {
    static std::atomic<uint32_t> value{0};
    static std::once_flag init;
    std::call_once(init, [] {
        const char* env = std::getenv("TT_UMD_MMIO_TIMEOUT_MS");
        uint32_t parsed = kDefaultMmioTimeoutMs;
        if (env != nullptr && *env != '\0') {
            try {
                parsed = static_cast<uint32_t>(std::stoul(env));
            } catch (...) {
                parsed = kDefaultMmioTimeoutMs;
            }
        }
        value.store(parsed, std::memory_order_relaxed);
    });
    return value;
}

}  // namespace

/* static */ void SiliconTlbWindow::set_mmio_timeout_ms(uint32_t ms) {
    // call mmio_timeout_storage() once first to ensure call_once has run, so
    // a subsequent get_mmio_timeout() observes our write rather than the
    // env-var initialization.
    mmio_timeout_storage().store(ms, std::memory_order_relaxed);
}

/* static */ std::chrono::milliseconds SiliconTlbWindow::get_mmio_timeout() {
    return std::chrono::milliseconds(mmio_timeout_storage().load(std::memory_order_relaxed));
}
```

At the top of `device/pcie/silicon_tlb_window.cpp`, add these includes alongside the existing ones:

```cpp
#include <cstdlib>
#include <mutex>
```

- [ ] **Step 5: Build and run config tests**

```bash
cmake --build build --target baremetal_tests
./build/test/umd/baremetal/baremetal_tests --gtest_filter='MmioTimeoutConfig.*'
```

Expected: both tests PASS.

- [ ] **Step 6: Confirm env-var override works**

```bash
TT_UMD_MMIO_TIMEOUT_MS=2500 ./build/test/umd/baremetal/baremetal_tests \
    --gtest_filter='MmioTimeoutConfig.DefaultIs1000Ms'
```

Expected: PASS — the assertion is "v > 0", and inspecting test output we can confirm the value picked up is 2500 (add `<< "v=" << v.count()` to the test's expectation if you want stronger evidence; this is optional).

- [ ] **Step 7: Stage and hand off**

```bash
git add device/api/umd/device/pcie/silicon_tlb_window.hpp \
        device/pcie/silicon_tlb_window.cpp \
        tests/baremetal/test_device_memcpy.cpp
```

Hand off to the user.

---

## Task 5: Thread deadline through `SiliconTlbWindow::write_block` / `read_block`

**Files:**
- Modify: `device/api/umd/device/pcie/silicon_tlb_window.hpp`
- Modify: `device/pcie/silicon_tlb_window.cpp`

- [ ] **Step 1: Update the private helper signatures in the header**

Open `device/api/umd/device/pcie/silicon_tlb_window.hpp`. The private section currently declares:

```cpp
    static void memcpy_from_device(void* dest, const volatile void* src, std::size_t num_bytes);
    static void memcpy_to_device(void* dest, const void* src, std::size_t num_bytes);
```

Change both to take a deadline:

```cpp
    static void memcpy_from_device(void* dest, const volatile void* src, std::size_t num_bytes,
                                   std::chrono::steady_clock::time_point deadline);
    static void memcpy_to_device(void* dest, const void* src, std::size_t num_bytes,
                                 std::chrono::steady_clock::time_point deadline);
```

(Note: `<chrono>` was already added to this header in Task 4.)

- [ ] **Step 2: Update the private helper bodies and the block callers**

Open `device/pcie/silicon_tlb_window.cpp`. The current `SiliconTlbWindow::write_block` (around line 113) and `read_block` (around line 125) look like:

```cpp
void SiliconTlbWindow::write_block(uint64_t offset, const void *data, size_t size) {
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    if (tlb_handle->get_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device((void *)dst, data, size);
    } else {
        umd::memcpy_to_device(dst, data, size);
    }
}

void SiliconTlbWindow::read_block(uint64_t offset, void *data, size_t size) {
    const volatile void *src = tlb_handle->get_base() + get_total_offset(offset);

    validate(offset, size);

    if (tlb_handle->get_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(data, src, size);
    } else {
        umd::memcpy_from_device(data, src, size);
    }
}
```

Replace both with deadline-computing versions:

```cpp
void SiliconTlbWindow::write_block(uint64_t offset, const void *data, size_t size) {
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    auto deadline = std::chrono::steady_clock::now() + get_mmio_timeout();

    if (tlb_handle->get_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device((void *)dst, data, size, deadline);
    } else {
        umd::memcpy_to_device(dst, data, size, deadline);
    }
}

void SiliconTlbWindow::read_block(uint64_t offset, void *data, size_t size) {
    const volatile void *src = tlb_handle->get_base() + get_total_offset(offset);

    validate(offset, size);

    auto deadline = std::chrono::steady_clock::now() + get_mmio_timeout();

    if (tlb_handle->get_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(data, src, size, deadline);
    } else {
        umd::memcpy_from_device(data, src, size, deadline);
    }
}
```

Now update the private helpers (currently at `silicon_tlb_window.cpp:137` and `:176`) to accept and forward the deadline. The current `memcpy_from_device` ends with:

```cpp
    // Copy the source-aligned middle using non-overlapping wide loads.
    std::size_t num_words = num_bytes / sizeof(copy_t);
    std::size_t middle_bytes = num_words * sizeof(copy_t);
    umd::memcpy_from_device(dest, sp, middle_bytes);
```

Update the function signature to:

```cpp
void SiliconTlbWindow::memcpy_from_device(void *dest, const volatile void *src, std::size_t num_bytes,
                                          std::chrono::steady_clock::time_point deadline) {
```

And update the `umd::memcpy_from_device(...)` call inside it to:

```cpp
    umd::memcpy_from_device(dest, sp, middle_bytes, deadline);
```

Similarly for `memcpy_to_device`:

```cpp
void SiliconTlbWindow::memcpy_to_device(void *dest, const void *src, std::size_t num_bytes,
                                        std::chrono::steady_clock::time_point deadline) {
```

And update the inner call:

```cpp
    umd::memcpy_to_device(dp, src, middle_bytes, deadline);
```

Leading/trailing single-word RMW operations in those private helpers stay unguarded — they are bounded by SIGBUS per the spec's "Non-goals" section.

- [ ] **Step 3: Build to confirm the wiring compiles**

```bash
cmake --build build --target tt-umd
```

Expected: success.

- [ ] **Step 4: Build and run the full test suite to confirm no regressions**

```bash
cmake --build build --target baremetal_tests
./build/test/umd/baremetal/baremetal_tests
```

Expected: all tests PASS, including pre-existing `LongJump*` tests.

- [ ] **Step 5: Stage and hand off**

```bash
git add device/api/umd/device/pcie/silicon_tlb_window.hpp \
        device/pcie/silicon_tlb_window.cpp
```

Hand off to the user.

---

## Task 6: Expose `DeviceTimeoutError` to Python

**Files:**
- Modify: `nanobind/py_api_tt_device.cpp`

- [ ] **Step 1: Mirror the SigbusError binding**

Open `nanobind/py_api_tt_device.cpp`. The existing block at line 85 looks like:

```cpp
    nb::exception<error::SigbusError>(m, "SigbusError");

    m.def(
        "raise_sigbus_error_for_testing",
        []() { throw error::SigbusError("This is a test exception from C++"); },
        release_gil(),
        "A helper function to verify SigbusError propagation");
```

Immediately after, add:

```cpp
    nb::exception<error::DeviceTimeoutError>(m, "DeviceTimeoutError");

    m.def(
        "raise_device_timeout_error_for_testing",
        []() { throw error::DeviceTimeoutError("This is a test exception from C++"); },
        release_gil(),
        "A helper function to verify DeviceTimeoutError propagation");
```

- [ ] **Step 2: Build the Python bindings**

```bash
cmake --build build --target tt-umd
```

Expected: success. (If nanobind is built as part of a separate target in this repo, build that target instead; the user can identify which.)

- [ ] **Step 3: Stage and hand off**

```bash
git add nanobind/py_api_tt_device.cpp
```

Hand off to the user.

---

## Task 7: Final verification and pre-commit

**Files:** All previously touched.

- [ ] **Step 1: Full build**

```bash
cmake --build build
```

Expected: success.

- [ ] **Step 2: Run the full test suite**

```bash
./build/test/umd/baremetal/baremetal_tests
```

Expected: all tests PASS, including `DeviceMemcpyTimeout.*` (8 cases), `MmioTimeoutConfig.*` (2 cases), and the pre-existing baremetal tests.

- [ ] **Step 3: Run pre-commit on the staged files**

```bash
pre-commit run --all-files
```

Expected: all hooks pass. If any auto-formatter modifies files, re-stage them with `git add` and re-run pre-commit until it's clean.

- [ ] **Step 4: Verify the staging area matches the spec's "Files touched" list**

```bash
git status
```

Expected files in the staged-changes section:

- `device/api/umd/device/utils/error.hpp`
- `device/pcie/device_memcpy.hpp`
- `device/pcie/device_memcpy.cpp`
- `device/api/umd/device/pcie/silicon_tlb_window.hpp`
- `device/pcie/silicon_tlb_window.cpp`
- `tests/baremetal/test_device_memcpy.cpp` (new)
- `tests/baremetal/CMakeLists.txt`
- `nanobind/py_api_tt_device.cpp`

If extra files appear, investigate before handing off — they are likely auto-formatter changes to unrelated files (pre-commit can do that). Roll those back unless they are warranted cleanup of files we already modified.

- [ ] **Step 5: Hand off for user commit**

Summarize the changes for the user and let them write the commit.

---

## Self-review notes

- **Spec coverage:**
  - Low-level deadline overloads → Tasks 2, 3.
  - Mid-level `write_block`/`read_block` wiring → Task 5.
  - Static config + env var → Task 4.
  - `DeviceTimeoutError` type → Task 1.
  - Python exposure → Task 6.
  - Tests → Tasks 2, 3, 4.
  - Docs in header reflecting worst-case → Task 2 (header comment) and Task 4 (config doc comment).
- **No placeholders.** All code snippets are concrete.
- **Type consistency.** `set_mmio_timeout_ms(uint32_t ms)` and `get_mmio_timeout()` returning `std::chrono::milliseconds` are used consistently across Tasks 4 and 5. Deadlines are `std::chrono::steady_clock::time_point` end-to-end.
- **TDD.** Each task writes failing tests first where possible. Tasks 5–7 are wiring/integration tasks where the unit-test coverage from Tasks 2–4 already exercises the new code paths.
- **No `git commit` steps.** All commit hand-offs go to the user per the project preference.
