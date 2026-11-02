// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The helper here carries __attribute__((target("arch=+v"))) rather than requiring a vector -march
// for the whole library: one riscv64 build has to run on both rv64gc and RVA23 hosts, so whether
// the vector form is used is decided at run time from HWCAP rather than at build time.
//
// Only device reads have a vector form. Vector stores measured 1.00x with strict ordering and
// ~1.1x relaxed, which does not pay for giving up the in-order 8-byte writes the scalar loop
// guarantees in front of the ARC bridge; see the discussion on #3762.

#include "device_memcpy_rvv.hpp"

#include <riscv_vector.h>
#include <sys/auxv.h>

#include <algorithm>
#include <cstdlib>

namespace tt::umd::rvv {

namespace {

// 1 << ('V' - 'A'), the kernel's COMPAT_HWCAP_ISA_V. Spelled out so this still builds against
// headers that predate the macro.
constexpr unsigned long HWCAP_ISA_V = 1UL << 21;

// Widest byte count one e8/LMUL=8 vector holds: VLMAX(e8, m8) == VLEN, so always a multiple of 8.
// Capping each iteration here keeps vl equal to the requested AVL, which matters because vsetvli
// is free to return any vl >= ceil(AVL/2) when VLMAX < AVL < 2*VLMAX -- a value that need not be a
// multiple of 8, and so could not be converted to a 64-bit element count without losing bytes.
__attribute__((target("arch=+v"))) inline std::size_t max_bytes() {
    static const std::size_t cached = __riscv_vsetvlmax_e8m8();
    return cached;
}

}  // namespace

// Stores never take a vector form, so this disables the vector loads in memcpy_from_device and
// nothing else. The scalar path is always correct, so it is a safe escape hatch if a platform
// turns out to decompose vector element accesses in a way its devices dislike.
bool enabled() {
    static const bool value =
        (getauxval(AT_HWCAP) & HWCAP_ISA_V) != 0 && std::getenv("TT_UMD_DISABLE_RVV_MEMCPY") == nullptr;
    return value;
}

// For a unit-stride access the element width comes from the vle64.v opcode itself rather than from
// vtype's SEW, so every element access is a well-formed 8-byte device read however the compiler
// encodes the preceding vsetvli -- which is the point, since the generic path copies through
// vector types declared aligned(1) and so reads the device a byte at a time. The host side uses
// byte elements, so `dest` needs no alignment.
//
// Element accesses within one unit-stride access are unordered: the V spec states that for a
// strongly ordered I/O region "the element accesses can be initiated in any order". Only the
// ordering *within* a single transfer is affected, which is the same latitude the x86 path takes
// with its AVX2 loads. The width is unaffected: each element access is 8 bytes, exactly as for ld.
__attribute__((target("arch=+v"))) void block_from_device(
    std::uint8_t* dest, const volatile std::uint8_t* src, std::size_t bytes) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    const auto* s = reinterpret_cast<const std::uint64_t*>(const_cast<const std::uint8_t*>(src));
    for (std::size_t remaining = bytes; remaining > 0;) {
        const std::size_t chunk = std::min(remaining, max_bytes());
        const std::size_t elements = chunk / sizeof(std::uint64_t);
        const vuint64m8_t data = __riscv_vle64_v_u64m8(s, elements);
        __riscv_vse8_v_u8m8(dest, __riscv_vreinterpret_v_u64m8_u8m8(data), chunk);
        s += elements;
        dest += chunk;
        remaining -= chunk;
    }
}

}  // namespace tt::umd::rvv
