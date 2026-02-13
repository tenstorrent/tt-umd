// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/silicon_tlb_window.hpp"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/exceptions.hpp"

namespace tt::umd {

static thread_local sigjmp_buf point __attribute__((tls_model("initial-exec")));
static thread_local volatile sig_atomic_t jump_set __attribute__((tls_model("initial-exec"))) = 0;

void sigbus_handler(int sig) {
    if (jump_set) {
        siglongjmp(point, 1);
    } else {
        _exit(sig);
    }
}

struct ScopedJumpGuard {
    ScopedJumpGuard() {
        jump_set = 1;
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    ~ScopedJumpGuard() {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        jump_set = 0;
    }
};

/* static */ void SiliconTlbWindow::set_sigbus_safe_handler(bool set_safe_handler) {
    if (set_safe_handler) {
        struct sigaction sa;
        sa.sa_handler = sigbus_handler;
        sigemptyset(&sa.sa_mask);
        // SA_NODEFER: Don't block SIGBUS after we longjmp out.
        sa.sa_flags = SA_NODEFER;

        if (sigaction(SIGBUS, &sa, nullptr) == -1) {
            perror("sigaction");
            _exit(1);
        }
        return;
    }
    signal(SIGBUS, SIG_DFL);
}

SiliconTlbWindow::SiliconTlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config) :
    TlbWindow(std::move(handle), config) {}

void SiliconTlbWindow::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));
    *reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset)) = value;
}

uint32_t SiliconTlbWindow::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    return *reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
}

void SiliconTlbWindow::write_register(uint64_t offset, const void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = static_cast<const uint32_t *>(data);
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    write_regs(dst, src, n);
}

void SiliconTlbWindow::read_register(uint64_t offset, void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = reinterpret_cast<const volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
    auto *dst = static_cast<uint32_t *>(data);

    validate(offset, size);

    read_regs((void *)src, n, (void *)dst);
}

void SiliconTlbWindow::write_block(uint64_t offset, const void *data, size_t size) {
    auto *src = static_cast<const uint32_t *>(data);
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    if (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device((void *)dst, src, size);
    } else {
        memcpy((void *)dst, (void *)src, size);
    }
}

void SiliconTlbWindow::read_block(uint64_t offset, void *data, size_t size) {
    auto *src = reinterpret_cast<const volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
    auto *dst = static_cast<uint32_t *>(data);

    validate(offset, size);

    if (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(dst, (void *)src, size);
    } else {
        memcpy((void *)dst, (void *)src, size);
    }
}

void SiliconTlbWindow::memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t *>(src_addr - src_misalignment);

        copy_t tmp = *sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
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
        memcpy(dp, &tmp, trailing_len);
    }
}

void SiliconTlbWindow::memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
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

        memcpy(reinterpret_cast<char *>(&tmp) + dest_misalignment, src, leading_len);
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

        memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

void SiliconTlbWindow::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void SiliconTlbWindow::read_regs(void *src_reg, uint32_t word_len, void *data) {
    const volatile uint32_t *src = reinterpret_cast<uint32_t *>(src_reg);
    uint32_t *dest = reinterpret_cast<uint32_t *>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
}

template <typename Func, typename... Args>
decltype(auto) SiliconTlbWindow::execute_safe(Func &&func, Args &&...args) {
    if (sigsetjmp(point, 1) == 0) {
        ScopedJumpGuard guard;
        return std::invoke(std::forward<Func>(func), this, std::forward<Args>(args)...);
    } else {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        jump_set = 0;
        throw SigbusError("SIGBUS signal detected: Device access failed.");
    }
}

void SiliconTlbWindow::safe_write32(uint64_t offset, uint32_t value) {
    execute_safe(&SiliconTlbWindow::write32, offset, value);
}

uint32_t SiliconTlbWindow::safe_read32(uint64_t offset) { return execute_safe(&SiliconTlbWindow::read32, offset); }

void SiliconTlbWindow::safe_write_register(uint64_t offset, const void *data, size_t size) {
    execute_safe(&SiliconTlbWindow::write_register, offset, data, size);
}

void SiliconTlbWindow::safe_read_register(uint64_t offset, void *data, size_t size) {
    execute_safe(&SiliconTlbWindow::read_register, offset, data, size);
}

void SiliconTlbWindow::safe_write_block(uint64_t offset, const void *data, size_t size) {
    execute_safe(&SiliconTlbWindow::write_block, offset, data, size);
}

void SiliconTlbWindow::safe_read_block(uint64_t offset, void *data, size_t size) {
    execute_safe(&SiliconTlbWindow::read_block, offset, data, size);
}

void SiliconTlbWindow::safe_write_block_reconfigure(
    const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    execute_safe(&SiliconTlbWindow::write_block_reconfigure, mem_ptr, core, addr, size, ordering);
}

void SiliconTlbWindow::safe_read_block_reconfigure(
    void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    execute_safe(&SiliconTlbWindow::read_block_reconfigure, mem_ptr, core, addr, size, ordering);
}

void SiliconTlbWindow::safe_noc_multicast_write_reconfigure(
    void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    execute_safe(&SiliconTlbWindow::noc_multicast_write_reconfigure, dst, size, core_start, core_end, addr, ordering);
}

}  // namespace tt::umd
