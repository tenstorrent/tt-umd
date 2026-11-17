// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_window.hpp"

#include <fmt/format.h>

#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include "common/utils.hpp"
#include "pcie/device_memcpy.hpp"
#include "tt-umd/driver_atomics.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Where a SIGBUS raised on this thread resumes: the innermost guarded window access in flight, or null when the
// thread is not inside one. Initial-exec TLS, so the signal handler reads it without the TLS resolver.
__attribute__((tls_model("initial-exec"))) thread_local sigjmp_buf* sigbus_jump_target = nullptr;

std::mutex sigbus_handler_mutex;
bool sigbus_handler_installed = false;

struct sigaction previous_sigbus_action {};

void sigbus_handler(int /*signal*/, siginfo_t* /*info*/, void* /*context*/) {
    if (sigjmp_buf* target = sigbus_jump_target) {
        siglongjmp(*target, 1);
    }
    // The fault did not come from a guarded access, so it is not ours to recover from. Returning re-runs the
    // faulting access, which then takes the default action.
    signal(SIGBUS, SIG_DFL);
}

// Arms a jump target for the duration of one guarded access, restoring the enclosing one (if any) afterwards, so
// that an access issued from inside another one -- a hang check probing the NOC -- keeps both guarded.
struct SigbusJumpScope {
    sigjmp_buf env;
    sigjmp_buf* const enclosing = sigbus_jump_target;

    SigbusJumpScope() = default;
    SigbusJumpScope(const SigbusJumpScope&) = delete;
    SigbusJumpScope& operator=(const SigbusJumpScope&) = delete;

    void arm() { sigbus_jump_target = &env; }

    ~SigbusJumpScope() { sigbus_jump_target = enclosing; }
};

}  // namespace

SiliconTlbWindow::SiliconTlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config, IoSafety io_safety) :
    TlbWindow(std::move(handle), config, io_safety) {
    update_io_timeout_callback();
}

template <typename Func, typename... Args>
decltype(auto) SiliconTlbWindow::execute_safe(Func&& func, Args&&... args) {
    if (io_safety_ != IoSafety::Enabled) {
        return std::invoke(std::forward<Func>(func), this, std::forward<Args>(args)...);
    }
    SigbusJumpScope scope;
    if (sigsetjmp(scope.env, 1) != 0) {
        throw error::SigbusError(fmt::format(
            "SIGBUS while accessing TLB window {}: the device was reset or stopped responding.",
            tlb_handle->get_tlb_id()));
    }
    scope.arm();
    return std::invoke(std::forward<Func>(func), this, std::forward<Args>(args)...);
}

void SiliconTlbWindow::write16(uint64_t offset, uint16_t value) {
    execute_safe(&SiliconTlbWindow::write16_impl, offset, value);
}

uint16_t SiliconTlbWindow::read16(uint64_t offset) { return execute_safe(&SiliconTlbWindow::read16_impl, offset); }

void SiliconTlbWindow::write32(uint64_t offset, uint32_t value) {
    execute_safe(&SiliconTlbWindow::write32_impl, offset, value);
}

uint32_t SiliconTlbWindow::read32(uint64_t offset) { return execute_safe(&SiliconTlbWindow::read32_impl, offset); }

void SiliconTlbWindow::write64(uint64_t offset, uint64_t value) {
    execute_safe(&SiliconTlbWindow::write64_impl, offset, value);
}

uint64_t SiliconTlbWindow::read64(uint64_t offset) { return execute_safe(&SiliconTlbWindow::read64_impl, offset); }

void SiliconTlbWindow::write_register(uint64_t offset, const void* data, size_t size) {
    execute_safe(&SiliconTlbWindow::write_register_impl, offset, data, size);
}

void SiliconTlbWindow::read_register(uint64_t offset, void* data, size_t size) {
    execute_safe(&SiliconTlbWindow::read_register_impl, offset, data, size);
}

void SiliconTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    execute_safe(&SiliconTlbWindow::write_block_impl, offset, data, size);
}

void SiliconTlbWindow::read_block(uint64_t offset, void* data, size_t size) {
    execute_safe(&SiliconTlbWindow::read_block_impl, offset, data, size);
}

void SiliconTlbWindow::set_io_timeout_hang_check(const std::function<bool(NocId)>& hang_check) {
    hang_check_ = hang_check;
    update_io_timeout_callback();
}

void SiliconTlbWindow::configure(const tlb_data& new_config) {
    // The configuration write is the first device access of a transfer, so it is guarded like the data accesses.
    execute_safe(&SiliconTlbWindow::configure_impl, new_config);
}

void SiliconTlbWindow::set_sigbus_safe_handler(bool set_safe_handler) {
    std::lock_guard<std::mutex> lock(sigbus_handler_mutex);
    if (set_safe_handler == sigbus_handler_installed) {
        return;
    }
    if (set_safe_handler) {
        struct sigaction action {};

        action.sa_sigaction = sigbus_handler;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGBUS, &action, &previous_sigbus_action) != 0) {
            UMD_THROW(error::RuntimeError, fmt::format("Failed to install the SIGBUS handler: {}.", strerror(errno)));
        }
    } else if (sigaction(SIGBUS, &previous_sigbus_action, nullptr) != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to restore the SIGBUS handler: {}.", strerror(errno)));
    }
    sigbus_handler_installed = set_safe_handler;
}

void SiliconTlbWindow::update_io_timeout_callback() {
    if (!hang_check_) {
        io_timeout_callback_ = [] { return true; };
        return;
    }
    const NocId noc_id = static_cast<NocId>(tlb_handle->get_config().noc_sel);
    io_timeout_callback_ = [hang_check = hang_check_, noc_id] { return !hang_check(noc_id); };
}

void SiliconTlbWindow::memcpy_from_device(
    void* dest, const volatile void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout) {
    tt::umd::memcpy_from_device(dest, src, num_bytes, on_timeout);
}

void SiliconTlbWindow::memcpy_to_device(
    void* dest, const void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout) {
    tt::umd::memcpy_to_device(dest, src, num_bytes, on_timeout);
    // Write-combined stores may otherwise linger in the CPU and land after a later access through another window.
    tt_driver_atomics::sfence();
}

void SiliconTlbWindow::write_regs(
    volatile uint32_t* dest, const uint32_t* src, uint32_t word_len, const std::function<bool()>& on_timeout) {
    for (uint32_t i = 0; i < word_len; i++) {
        uint32_t value = 0;
        std::memcpy(&value, src + i, sizeof(value));
        write32_to_device(dest + i, value, on_timeout);
    }
    tt_driver_atomics::sfence();
}

void SiliconTlbWindow::read_regs(
    void* src_reg, uint32_t word_len, void* data, const std::function<bool()>& on_timeout) {
    const volatile uint32_t* src = static_cast<const volatile uint32_t*>(src_reg);
    for (uint32_t i = 0; i < word_len; i++) {
        const uint32_t value = read32_from_device(src + i, on_timeout);
        std::memcpy(static_cast<uint8_t*>(data) + i * sizeof(uint32_t), &value, sizeof(value));
    }
}

void SiliconTlbWindow::write16_impl(uint64_t offset, uint16_t value) {
    validate(offset, sizeof(value));
    write16_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
    tt_driver_atomics::sfence();
}

uint16_t SiliconTlbWindow::read16_impl(uint64_t offset) {
    validate(offset, sizeof(uint16_t));
    return read16_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write32_impl(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(value));
    write32_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
    tt_driver_atomics::sfence();
}

uint32_t SiliconTlbWindow::read32_impl(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    return read32_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write64_impl(uint64_t offset, uint64_t value) {
    validate(offset, sizeof(value));
    write64_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
    tt_driver_atomics::sfence();
}

uint64_t SiliconTlbWindow::read64_impl(uint64_t offset) {
    validate(offset, sizeof(uint64_t));
    return read64_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write_register_impl(uint64_t offset, const void* data, size_t size) {
    validate_register_access(offset, size);
    validate(offset, size);
    write_regs(
        reinterpret_cast<volatile uint32_t*>(tlb_handle->get_base() + get_total_offset(offset)),
        static_cast<const uint32_t*>(data),
        static_cast<uint32_t>(size / sizeof(uint32_t)),
        io_timeout_callback_);
}

void SiliconTlbWindow::read_register_impl(uint64_t offset, void* data, size_t size) {
    validate_register_access(offset, size);
    validate(offset, size);
    read_regs(
        tlb_handle->get_base() + get_total_offset(offset),
        static_cast<uint32_t>(size / sizeof(uint32_t)),
        data,
        io_timeout_callback_);
}

void SiliconTlbWindow::write_block_impl(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    memcpy_to_device(tlb_handle->get_base() + get_total_offset(offset), data, size, io_timeout_callback_);
}

void SiliconTlbWindow::read_block_impl(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    memcpy_from_device(data, tlb_handle->get_base() + get_total_offset(offset), size, io_timeout_callback_);
}

void SiliconTlbWindow::configure_impl(const tlb_data& new_config) {
    TlbWindow::configure(new_config);
    update_io_timeout_callback();
}

}  // namespace tt::umd
