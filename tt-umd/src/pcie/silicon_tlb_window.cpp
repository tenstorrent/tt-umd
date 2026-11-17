// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_window.hpp"

#include <fmt/format.h>

#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <cstring>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

#include "pcie/device_memcpy.hpp"
#include "tt-umd/driver_atomics.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Where a SIGBUS raised by this thread's guarded window access resumes, or null outside one.
thread_local sigjmp_buf* sigbus_resume_point = nullptr;

std::mutex sigbus_handler_mutex;

void sigbus_safe_handler(int signal_number, siginfo_t* /*info*/, void* /*context*/) {
    if (sigbus_resume_point != nullptr) {
        siglongjmp(*sigbus_resume_point, 1);
    }
    // The fault came from an unguarded access: let it take the process down as it would have anyway.
    std::signal(signal_number, SIG_DFL);
    std::raise(signal_number);
}

void check_register_access(uint64_t offset, size_t size) {
    if (offset % sizeof(uint32_t) != 0 || size % sizeof(uint32_t) != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Register access of {} bytes at offset {:#x} must be 4-byte aligned in address and size.",
                size,
                offset));
    }
}

// Write-combined stores can sit in the CPU's buffers indefinitely, so every write through a WC window
// ends with a store fence: once a write returns it is on its way to the device.
void flush_if_write_combined(const TlbHandle& handle) {
    if (handle.get_tlb_mapping() == TlbMapping::WC) {
        tt_driver_atomics::sfence();
    }
}

}  // namespace

SiliconTlbWindow::SiliconTlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config, IoSafety io_safety) :
    TlbWindow(std::move(handle), config, io_safety) {
    update_io_timeout_callback();
}

template <typename Func, typename... Args>
decltype(auto) SiliconTlbWindow::execute_safe(Func&& func, Args&&... args) {
    if (io_safety_ == IoSafety::Disabled) {
        return std::invoke(std::forward<Func>(func), this, std::forward<Args>(args)...);
    }

    sigjmp_buf resume_point;
    sigjmp_buf* const outer_resume_point = sigbus_resume_point;
    // Saving the signal mask lets the jump out of the handler unblock SIGBUS again.
    if (sigsetjmp(resume_point, 1) != 0) {
        sigbus_resume_point = outer_resume_point;
        throw error::SigbusError(
            fmt::format("SIGBUS while accessing TLB window {}; the device mapping is gone.", tlb_handle->get_tlb_id()));
    }
    sigbus_resume_point = &resume_point;
    try {
        if constexpr (std::is_void_v<std::invoke_result_t<Func, SiliconTlbWindow*, Args...>>) {
            std::invoke(std::forward<Func>(func), this, std::forward<Args>(args)...);
            sigbus_resume_point = outer_resume_point;
        } else {
            auto result = std::invoke(std::forward<Func>(func), this, std::forward<Args>(args)...);
            sigbus_resume_point = outer_resume_point;
            return result;
        }
    } catch (...) {
        sigbus_resume_point = outer_resume_point;
        throw;
    }
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
    // The configuration write is the first device access of a transfer, so it is guarded too.
    execute_safe(&SiliconTlbWindow::configure_impl, new_config);
}

void SiliconTlbWindow::set_sigbus_safe_handler(bool set_safe_handler) {
    std::lock_guard<std::mutex> lock(sigbus_handler_mutex);

    struct sigaction action {};

    if (set_safe_handler) {
        action.sa_sigaction = sigbus_safe_handler;
        action.sa_flags = SA_SIGINFO;
    } else {
        action.sa_handler = SIG_DFL;
    }
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGBUS, &action, nullptr) != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to set the SIGBUS handler: {}", std::strerror(errno)));
    }
}

void SiliconTlbWindow::update_io_timeout_callback() {
    if (!hang_check_) {
        io_timeout_callback_ = [] { return true; };
        return;
    }
    const NocId noc_id = static_cast<NocId>(tlb_handle->get_config().noc_sel);
    io_timeout_callback_ = [this, noc_id] { return !hang_check_(noc_id); };
}

void SiliconTlbWindow::memcpy_from_device(
    void* dest, const volatile void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout) {
    ::tt::umd::memcpy_from_device(dest, src, num_bytes, on_timeout);
}

void SiliconTlbWindow::memcpy_to_device(
    void* dest, const void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout) {
    ::tt::umd::memcpy_to_device(dest, src, num_bytes, on_timeout);
}

void SiliconTlbWindow::write_regs(
    volatile uint32_t* dest, const uint32_t* src, uint32_t word_len, const std::function<bool()>& on_timeout) {
    for (uint32_t i = 0; i < word_len; i++) {
        uint32_t value = 0;
        std::memcpy(&value, src + i, sizeof(value));
        write32_to_device(dest + i, value, on_timeout);
    }
}

void SiliconTlbWindow::read_regs(
    void* src_reg, uint32_t word_len, void* data, const std::function<bool()>& on_timeout) {
    const auto* src = static_cast<const volatile uint32_t*>(src_reg);
    auto* dst = static_cast<uint8_t*>(data);
    for (uint32_t i = 0; i < word_len; i++) {
        const uint32_t value = read32_from_device(src + i, on_timeout);
        std::memcpy(dst + i * sizeof(uint32_t), &value, sizeof(value));
    }
}

void SiliconTlbWindow::write16_impl(uint64_t offset, uint16_t value) {
    validate(offset, sizeof(value));
    write16_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
    flush_if_write_combined(*tlb_handle);
}

uint16_t SiliconTlbWindow::read16_impl(uint64_t offset) {
    validate(offset, sizeof(uint16_t));
    return read16_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write32_impl(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(value));
    write32_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
    flush_if_write_combined(*tlb_handle);
}

uint32_t SiliconTlbWindow::read32_impl(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    return read32_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write64_impl(uint64_t offset, uint64_t value) {
    validate(offset, sizeof(value));
    write64_to_device(tlb_handle->get_base() + get_total_offset(offset), value, io_timeout_callback_);
    flush_if_write_combined(*tlb_handle);
}

uint64_t SiliconTlbWindow::read64_impl(uint64_t offset) {
    validate(offset, sizeof(uint64_t));
    return read64_from_device(tlb_handle->get_base() + get_total_offset(offset), io_timeout_callback_);
}

void SiliconTlbWindow::write_register_impl(uint64_t offset, const void* data, size_t size) {
    check_register_access(offset, size);
    validate(offset, size);
    write_regs(
        reinterpret_cast<volatile uint32_t*>(tlb_handle->get_base() + get_total_offset(offset)),
        static_cast<const uint32_t*>(data),
        static_cast<uint32_t>(size / sizeof(uint32_t)),
        io_timeout_callback_);
    flush_if_write_combined(*tlb_handle);
}

void SiliconTlbWindow::read_register_impl(uint64_t offset, void* data, size_t size) {
    check_register_access(offset, size);
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
    flush_if_write_combined(*tlb_handle);
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
