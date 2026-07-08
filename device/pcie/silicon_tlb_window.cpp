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
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

#include "device_memcpy.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

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
    TlbWindow(std::move(handle), config) {
    update_io_timeout_callback();
}

void SiliconTlbWindow::set_io_timeout_hang_check(const std::function<bool(NocId)> &hang_check) {
    hang_check_ = hang_check;
    update_io_timeout_callback();
}

void SiliconTlbWindow::configure(const tlb_data &new_config) {
    TlbWindow::configure(new_config);
    update_io_timeout_callback();
}

void SiliconTlbWindow::update_io_timeout_callback() {
    if (!hang_check_) {
        io_timeout_callback_ = {};
        return;
    }
    // The live TLB config's noc_sel is whatever the last (re)configure selected, so it identifies the
    // in-flight op's NOC. is_false_alarm semantics (OpTimeoutGuard): healthy NOC => true (ignore the
    // overrun), hung NOC => false (confirm it and abort with DeviceTimeoutError).
    const NocId noc = static_cast<NocId>(handle_ref().get_config().noc_sel);
    auto hang_check = hang_check_;
    io_timeout_callback_ = [hang_check, noc]() -> bool { return !hang_check(noc); };
}

void SiliconTlbWindow::write16(uint64_t offset, uint16_t value) {
    validate(offset, sizeof(uint16_t));
    write16_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
}

uint16_t SiliconTlbWindow::read16(uint64_t offset) {
    validate(offset, sizeof(uint16_t));
    return read16_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));
    write32_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
}

uint32_t SiliconTlbWindow::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    return read32_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write_register(uint64_t offset, const void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = static_cast<const uint32_t *>(data);
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    write_regs(dst, src, n, io_timeout_callback_);
}

void SiliconTlbWindow::read_register(uint64_t offset, void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = reinterpret_cast<const volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
    auto *dst = static_cast<uint32_t *>(data);

    validate(offset, size);

    read_regs((void *)src, n, (void *)dst, io_timeout_callback_);
}

void SiliconTlbWindow::write_block(uint64_t offset, const void *data, size_t size) {
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    if (tlb_handle->get_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device((void *)dst, data, size, io_timeout_callback_);
    } else {
        umd::memcpy_to_device(dst, data, size, io_timeout_callback_);
    }
}

void SiliconTlbWindow::read_block(uint64_t offset, void *data, size_t size) {
    const volatile void *src = tlb_handle->get_base() + get_total_offset(offset);

    validate(offset, size);

    if (tlb_handle->get_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(data, src, size, io_timeout_callback_);
    } else {
        umd::memcpy_from_device(data, src, size, io_timeout_callback_);
    }
}

void SiliconTlbWindow::memcpy_from_device(
    void *dest, const volatile void *src, std::size_t num_bytes, const std::function<bool()> &on_timeout) {
    using copy_t = std::uint32_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t *>(src_addr - src_misalignment);

        copy_t tmp;
        umd::memcpy_from_device(&tmp, sp, sizeof(tmp), on_timeout);
        sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
        num_bytes -= leading_len;
        dest = static_cast<char *>(dest) + leading_len;

    } else {
        sp = static_cast<const volatile copy_t *>(src);
    }

    // Copy the source-aligned middle using non-overlapping wide loads.
    std::size_t num_words = num_bytes / sizeof(copy_t);
    std::size_t middle_bytes = num_words * sizeof(copy_t);
    umd::memcpy_from_device(dest, sp, middle_bytes, on_timeout);

    auto *dp = static_cast<char *>(dest) + middle_bytes;
    sp += num_words;

    // Finally copy any sub-word trailer.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp;
        umd::memcpy_from_device(&tmp, sp, sizeof(tmp), on_timeout);
        memcpy(dp, &tmp, trailing_len);
    }
}

void SiliconTlbWindow::memcpy_to_device(
    void *dest, const void *src, std::size_t num_bytes, const std::function<bool()> &on_timeout) {
    using copy_t = std::uint32_t;

    // Start by aligning the destination (device) pointer. If needed, do RMW to fix up the
    // first partial word.
    volatile copy_t *dp;

    std::uintptr_t dest_addr = reinterpret_cast<std::uintptr_t>(dest);
    unsigned int dest_misalignment = dest_addr % sizeof(copy_t);

    if (dest_misalignment != 0) {
        // Read-modify-write for the first dest element.
        dp = reinterpret_cast<copy_t *>(dest_addr - dest_misalignment);

        copy_t tmp;
        umd::memcpy_from_device(&tmp, dp, sizeof(tmp), on_timeout);

        auto leading_len = std::min(sizeof(tmp) - dest_misalignment, num_bytes);

        memcpy(reinterpret_cast<char *>(&tmp) + dest_misalignment, src, leading_len);
        num_bytes -= leading_len;
        src = static_cast<const char *>(src) + leading_len;

        umd::memcpy_to_device(dp, &tmp, sizeof(tmp), on_timeout);
        dp++;

    } else {
        dp = static_cast<copy_t *>(dest);
    }

    // Copy the destination-aligned middle using non-overlapping wide stores.
    std::size_t num_words = num_bytes / sizeof(copy_t);
    std::size_t middle_bytes = num_words * sizeof(copy_t);
    umd::memcpy_to_device(dp, src, middle_bytes, on_timeout);

    dp += num_words;
    auto *sp = static_cast<const char *>(src) + middle_bytes;

    // Finally copy any sub-word trailer, again RMW on the destination.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp;
        umd::memcpy_from_device(&tmp, dp, sizeof(tmp), on_timeout);

        memcpy(&tmp, sp, trailing_len);

        umd::memcpy_to_device(dp, &tmp, sizeof(tmp), on_timeout);
    }
}

void SiliconTlbWindow::write_regs(
    volatile uint32_t *dest, const uint32_t *src, uint32_t word_len, const std::function<bool()> &on_timeout) {
    while (word_len-- != 0) {
        write32_to_device(dest++, *src++, on_timeout);
    }
}

void SiliconTlbWindow::read_regs(
    void *src_reg, uint32_t word_len, void *data, const std::function<bool()> &on_timeout) {
    auto *src = static_cast<const volatile uint32_t *>(src_reg);
    auto *dest = reinterpret_cast<uint32_t *>(data);

    while (word_len-- != 0) {
        uint32_t temp = read32_from_device(src++, on_timeout);
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
        throw error::SigbusError("SIGBUS signal detected: Device access failed.");
    }
}

void SiliconTlbWindow::safe_write16(uint64_t offset, uint16_t value) {
    execute_safe(&SiliconTlbWindow::write16, offset, value);
}

uint16_t SiliconTlbWindow::safe_read16(uint64_t offset) { return execute_safe(&SiliconTlbWindow::read16, offset); }

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
    const void *mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    execute_safe(&SiliconTlbWindow::write_block_reconfigure, mem_ptr, core, addr, size, noc_id, ordering);
}

void SiliconTlbWindow::safe_read_block_reconfigure(
    void *mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    execute_safe(&SiliconTlbWindow::read_block_reconfigure, mem_ptr, core, addr, size, noc_id, ordering);
}

void SiliconTlbWindow::safe_read_register_reconfigure(
    void *mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    execute_safe(&SiliconTlbWindow::read_register_reconfigure, mem_ptr, core, addr, size, noc_id, ordering);
}

void SiliconTlbWindow::safe_write_register_reconfigure(
    const void *mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    execute_safe(&SiliconTlbWindow::write_register_reconfigure, mem_ptr, core, addr, size, noc_id, ordering);
}

void SiliconTlbWindow::safe_noc_multicast_write_reconfigure(
    const void *src,
    size_t size,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t addr,
    NocId noc_id,
    uint64_t ordering) {
    execute_safe(
        &SiliconTlbWindow::noc_multicast_write_reconfigure, src, size, core_start, core_end, addr, noc_id, ordering);
}

}  // namespace tt::umd
