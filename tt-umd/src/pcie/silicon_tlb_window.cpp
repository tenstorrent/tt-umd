// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_window.hpp"

#include <utility>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

SiliconTlbWindow::SiliconTlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config, IoSafety io_safety) :
    TlbWindow(std::move(handle), config, io_safety) {}

void SiliconTlbWindow::write16(uint64_t offset, uint16_t value) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write16 is not implemented yet");
}

uint16_t SiliconTlbWindow::read16(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read16 is not implemented yet");
}

void SiliconTlbWindow::write32(uint64_t offset, uint32_t value) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write32 is not implemented yet");
}

uint32_t SiliconTlbWindow::read32(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read32 is not implemented yet");
}

void SiliconTlbWindow::write64(uint64_t offset, uint64_t value) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write64 is not implemented yet");
}

uint64_t SiliconTlbWindow::read64(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read64 is not implemented yet");
}

void SiliconTlbWindow::write_register(uint64_t offset, const void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write_register is not implemented yet");
}

void SiliconTlbWindow::read_register(uint64_t offset, void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read_register is not implemented yet");
}

void SiliconTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write_block is not implemented yet");
}

void SiliconTlbWindow::read_block(uint64_t offset, void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read_block is not implemented yet");
}

void SiliconTlbWindow::set_io_timeout_hang_check(const std::function<bool(NocId)>& hang_check) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::set_io_timeout_hang_check is not implemented yet");
}

void SiliconTlbWindow::configure(const tlb_data& new_config) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::configure is not implemented yet");
}

void SiliconTlbWindow::set_sigbus_safe_handler(bool set_safe_handler) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::set_sigbus_safe_handler is not implemented yet");
}

void SiliconTlbWindow::update_io_timeout_callback() {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::update_io_timeout_callback is not implemented yet");
}

void SiliconTlbWindow::memcpy_from_device(
    void* dest, const volatile void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::memcpy_from_device is not implemented yet");
}

void SiliconTlbWindow::memcpy_to_device(
    void* dest, const void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::memcpy_to_device is not implemented yet");
}

void SiliconTlbWindow::write_regs(
    volatile uint32_t* dest, const uint32_t* src, uint32_t word_len, const std::function<bool()>& on_timeout) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write_regs is not implemented yet");
}

void SiliconTlbWindow::read_regs(
    void* src_reg, uint32_t word_len, void* data, const std::function<bool()>& on_timeout) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read_regs is not implemented yet");
}

void SiliconTlbWindow::write16_impl(uint64_t offset, uint16_t value) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write16_impl is not implemented yet");
}

uint16_t SiliconTlbWindow::read16_impl(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read16_impl is not implemented yet");
}

void SiliconTlbWindow::write32_impl(uint64_t offset, uint32_t value) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write32_impl is not implemented yet");
}

uint32_t SiliconTlbWindow::read32_impl(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read32_impl is not implemented yet");
}

void SiliconTlbWindow::write64_impl(uint64_t offset, uint64_t value) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write64_impl is not implemented yet");
}

uint64_t SiliconTlbWindow::read64_impl(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read64_impl is not implemented yet");
}

void SiliconTlbWindow::write_register_impl(uint64_t offset, const void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write_register_impl is not implemented yet");
}

void SiliconTlbWindow::read_register_impl(uint64_t offset, void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read_register_impl is not implemented yet");
}

void SiliconTlbWindow::write_block_impl(uint64_t offset, const void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::write_block_impl is not implemented yet");
}

void SiliconTlbWindow::read_block_impl(uint64_t offset, void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::read_block_impl is not implemented yet");
}

void SiliconTlbWindow::configure_impl(const tlb_data& new_config) {
    UMD_THROW(error::RuntimeError, "SiliconTlbWindow::configure_impl is not implemented yet");
}

}  // namespace tt::umd
