// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/tlb_window.hpp"

#include <string.h>

#include <stdexcept>

#include "assert.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "utils.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#endif

#define LOAD_STREAM_32()                                                                 \
    do {                                                                                 \
        _mm256_stream_si256((__m256i *)dst8, _mm256_loadu_si256((const __m256i *)src8)); \
        src8 += sizeof(__m256i);                                                         \
        dst8 += sizeof(__m256i);                                                         \
    } while (0)

#define LOAD_STREAM_16()                                                           \
    do {                                                                           \
        _mm_stream_si128((__m128i *)dst8, _mm_loadu_si128((const __m128i *)src8)); \
        src8 += sizeof(__m128i);                                                   \
        dst8 += sizeof(__m128i);                                                   \
    } while (0)

#define LOAD_STREAM_4()                                     \
    do {                                                    \
        _mm_stream_si32((int32_t *)dst8, *(int32_t *)src8); \
        src8 += sizeof(int32_t);                            \
        dst8 += sizeof(int32_t);                            \
    } while (0)

#define LOAD_STREAM_4_UNALIGNED()                 \
    do {                                          \
        int32_t val = 0;                          \
        std::memcpy(&val, src8, sizeof(int32_t)); \
        _mm_stream_si32((int32_t *)dst8, val);    \
        src8 += sizeof(int32_t);                  \
        dst8 += sizeof(int32_t);                  \
    } while (0)

static constexpr uint32_t MEMCPY_ALIGNMENT = 16;

extern bool umd_use_noc1;

namespace tt::umd {

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config) : tlb_handle(std::move(handle)) {
    tlb_data aligned_config = config;
    aligned_config.local_offset = config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = config.local_offset - (config.local_offset & ~(tlb_handle->get_size() - 1));
}

void TlbWindow::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));
    *reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset)) = value;
}

uint32_t TlbWindow::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    return *reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
}

void TlbWindow::write_register(uint64_t offset, const void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = static_cast<const uint32_t *>(data);
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    write_regs(dst, src, n);
}

void TlbWindow::read_register(uint64_t offset, void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = reinterpret_cast<const volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
    auto *dst = static_cast<uint32_t *>(data);

    validate(offset, size);

    read_regs((void *)src, n, (void *)dst);
}

void TlbWindow::write_block(uint64_t offset, const void *data, size_t size) {
    auto *src = static_cast<const uint32_t *>(data);
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    // if (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0) {
    //     memcpy_to_device((void *)dst, src, size);
    // } else {
    //     memcpy((void *)dst, (void *)src, size);
    // }

    bool use_safe_memcpy = false;
    if constexpr (is_arm_platform() || is_riscv_platform()) {
        use_safe_memcpy = true;
    } else {
        use_safe_memcpy = (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0);
    }
    if (use_safe_memcpy) {
        memcpy_to_device((void *)dst, (void *)src, size);
    } else {
        custom_memcpy((void *)dst, (void *)src, size);
    }
}

void TlbWindow::read_block(uint64_t offset, void *data, size_t size) {
    auto *src = reinterpret_cast<const volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
    auto *dst = static_cast<uint32_t *>(data);

    validate(offset, size);

    // if (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0) {
    //     memcpy_from_device(dst, (void *)src, size);
    // } else {
    //     memcpy((void *)dst, (void *)src, size);
    // }

    // void *dest = reinterpret_cast<void *>(buffer_addr);
    bool use_safe_memcpy = false;
    if constexpr (is_arm_platform() || is_riscv_platform()) {
        use_safe_memcpy = true;
    } else {
        use_safe_memcpy = (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0);
    }
    if (use_safe_memcpy) {
        memcpy_from_device((void *)dst, (void *)src, size);
    } else {
        custom_memcpy((void *)dst, (void *)src, size);
    }

    // if (num_bytes >= sizeof(std::uint32_t)) {
    //     // TODO: looks like there is potential for undefined behavior here.
    //     detect_hang_read(*reinterpret_cast<std::uint32_t *>(dest));
    // }
}

void TlbWindow::read_block_reconfigure(
    void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    uint8_t *buffer_addr = static_cast<uint8_t *>(mem_ptr);
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = ordering;
    config.static_vc = (PCIDevice::get_pcie_arch() == tt::ARCH::BLACKHOLE) ? false : true;

    while (size > 0) {
        configure(config);
        uint32_t tlb_size = get_size();
        uint32_t transfer_size = std::min(size, tlb_size);

        read_block(0, buffer_addr, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;

        config.local_offset = addr;
    }
}

void TlbWindow::write_block_reconfigure(
    const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    const uint8_t *buffer_addr = static_cast<const uint8_t *>(mem_ptr);
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = ordering;
    config.static_vc = (PCIDevice::get_pcie_arch() == tt::ARCH::BLACKHOLE) ? false : true;

    while (size > 0) {
        configure(config);
        uint32_t tlb_size = get_size();

        uint32_t transfer_size = std::min(size, tlb_size);

        write_block(0, buffer_addr, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;

        config.local_offset = addr;
    }
}

TlbHandle &TlbWindow::handle_ref() const { return *tlb_handle; }

size_t TlbWindow::get_size() const { return tlb_handle->get_size() - offset_from_aligned_addr; }

void TlbWindow::validate(uint64_t offset, size_t size) const {
    if ((offset + size) > get_size()) {
        throw std::out_of_range("Out of bounds access");
    }
}

void TlbWindow::configure(const tlb_data &new_config) {
    tlb_data aligned_config = new_config;
    aligned_config.local_offset = new_config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = new_config.local_offset - (new_config.local_offset & ~(tlb_handle->get_size() - 1));
}

uint64_t TlbWindow::get_total_offset(uint64_t offset) const { return offset + offset_from_aligned_addr; }

void TlbWindow::custom_memcpy(void *dst, const void *src, std::size_t size) {
    if (size == 0) {
        return;
    }

    uint8_t *d = static_cast<uint8_t *>(dst);
    const uint8_t *s = static_cast<const uint8_t *>(src);

    uintptr_t addr = reinterpret_cast<uintptr_t>(d);
    size_t misalign = addr % 16;

    if (misalign != 0) {
        size_t prefix = 16 - misalign;
        if (prefix > size) {
            prefix = size;
        }

        std::memcpy(d, s, prefix);
        d += prefix;
        s += prefix;
        size -= prefix;
    }

    size_t aligned_size = size & ~size_t(15);  // round down to multiple of 16
    if (aligned_size > 0) {
        custom_memcpy_aligned(d, s, aligned_size);
        d += aligned_size;
        s += aligned_size;
        size -= aligned_size;
    }

    if (size > 0) {
        std::memcpy(d, s, size);
    }
}

void TlbWindow::custom_memcpy_aligned(void *dst, const void *src, std::size_t n) {
#if defined(__x86_64__) || defined(__i386__)
    // Ensure destination is properly aligned for optimal SIMD performance.
    TT_ASSERT((uintptr_t)dst % MEMCPY_ALIGNMENT == 0);

    // Configuration for bulk processing: inner loop processes 8 x 32-byte operations
    // This creates 256-byte blocks (8 * 32 = 256 bytes) for maximum throughput.
    constexpr uint32_t inner_loop = 8;
    constexpr uint32_t inner_blk_size = inner_loop * sizeof(__m256i);  // 256 bytes

    const auto *src8 = static_cast<const uint8_t *>(src);
    auto *dst8 = static_cast<uint8_t *>(dst);

    size_t num_lines = n / inner_blk_size;  // Number of 256-byte blocks to process

    // PHASE 1: Process 256-byte blocks 32 bytes at a time
    // This is the main bulk processing phase for maximum efficiency.
    if (num_lines > 0) {
        // Handle potential misalignment by processing a single 16-byte chunk first
        // This ensures subsequent 32-byte operations are properly aligned
        // WARNING: This does not cover the case where dst is not 16-byte aligned.
        if ((uintptr_t)dst8 % sizeof(__m256i) != 0) {
            LOAD_STREAM_16();
            n -= sizeof(__m128i);
            num_lines = n / inner_blk_size;  // Recalculate after alignment adjustment
        }

        // Main bulk processing loop: Each iteration processes a 256 byte block. Blocks are processed 32 bytes at a
        // time.
        for (size_t i = 0; i < num_lines; ++i) {
            for (size_t j = 0; j < inner_loop; ++j) {
                LOAD_STREAM_32();
            }
            n -= inner_blk_size;
        }
    }

    // PHASE 2: Process remaining data that doesn't fill a complete 256-byte block.
    if (n > 0) {
        // Phase 2.1: Process remaining 32-byte chunks.
        num_lines = n / sizeof(__m256i);  // Number of 32-byte blocks to process
        if (num_lines > 0) {
            // Handle alignment for 32-byte operations if needed
            // WARNING: This does not cover the case where dst is not 16-byte aligned.
            if ((uintptr_t)dst8 % sizeof(__m256i) != 0) {
                LOAD_STREAM_16();
                n -= sizeof(__m128i);
                num_lines = n / sizeof(__m256i);  // Recalculate after alignment adjustment
            }

            // Process individual 32-byte blocks.
            for (size_t i = 0; i < num_lines; ++i) {
                LOAD_STREAM_32();
            }
            n -= num_lines * sizeof(__m256i);
        }

        // PHASE 2.2: Process remaining 16-byte chunks.
        num_lines = n / sizeof(__m128i);  // Number of 16-byte blocks to process
        if (num_lines > 0) {
            for (size_t i = 0; i < num_lines; ++i) {
                LOAD_STREAM_16();
            }
            n -= num_lines * sizeof(__m128i);
        }

        // PHASE 2.3: Process remaining 4-byte chunks.
        num_lines = n / sizeof(int32_t);  // Number of 4-byte blocks to process
        if (num_lines > 0) {
            if ((uintptr_t)src8 % sizeof(int32_t) != 0) {
                for (size_t i = 0; i < num_lines; ++i) {
                    LOAD_STREAM_4_UNALIGNED();
                }
            } else {
                for (size_t i = 0; i < num_lines; ++i) {
                    LOAD_STREAM_4();
                }
            }
            n -= num_lines * sizeof(int32_t);
        }

        // PHASE 2.4: Handle the final few bytes (< 4 bytes)
        // We are the ones in control of and allocating the dst buffer,
        // so writing a few bytes extra is okay because we are guaranteeing the size is adequate.
        if (n > 0) {
            int32_t val = 0;
            std::memcpy(&val, src8, n);
            _mm_stream_si32((int32_t *)dst8, val);
        }
    }

    // Optional memory fence for debugging/synchronization
    // Ensures all streaming stores complete before function returns
    // if constexpr (debug_sync) {
    tt_driver_atomics::sfence();
// }
#else
    std::memcpy(dest, src, num_bytes);
#endif
}

void TlbWindow::memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the destination (device) pointer. If needed, do RMW to fix up the
    // first partial word.
    volatile copy_t *dp;

    std::uintptr_t dest_addr = reinterpret_cast<std::uintptr_t>(dest);
    unsigned int dest_misalignment = dest_addr % sizeof(copy_t);

    if (dest_misalignment != 0) {
        // Read-modify-write for the first dest element.
        dp = reinterpret_cast<copy_t *>(dest_addr - dest_misalignment);

        copy_t tmp = *dp;

        auto leading_len = std::min(sizeof(tmp) - dest_misalignment, num_bytes);

        custom_memcpy(reinterpret_cast<char *>(&tmp) + dest_misalignment, src, leading_len);
        num_bytes -= leading_len;
        src = static_cast<const char *>(src) + leading_len;

        *dp++ = tmp;

    } else {
        dp = static_cast<copy_t *>(dest);
    }

    // Copy the destination-aligned middle.
    const copy_t *sp = static_cast<const copy_t *>(src);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer, again RMW on the destination.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *dp;

        custom_memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

void TlbWindow::memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t *>(src_addr - src_misalignment);

        copy_t tmp = *sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        custom_memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
        num_bytes -= leading_len;
        dest = static_cast<char *>(dest) + leading_len;

    } else {
        sp = static_cast<const volatile copy_t *>(src);
    }

    // Copy the source-aligned middle.
    copy_t *dp = static_cast<copy_t *>(dest);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *sp;
        custom_memcpy(dp, &tmp, trailing_len);
    }
}

uint64_t TlbWindow::get_base_address() const {
    return handle_ref().get_config().local_offset + offset_from_aligned_addr;
}

void TlbWindow::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void TlbWindow::read_regs(void *src_reg, uint32_t word_len, void *data) {
    const volatile uint32_t *src = reinterpret_cast<uint32_t *>(src_reg);
    uint32_t *dest = reinterpret_cast<uint32_t *>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
}

}  // namespace tt::umd
