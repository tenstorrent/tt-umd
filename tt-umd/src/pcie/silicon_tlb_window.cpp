// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_window.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "common/unsupported.hpp"

namespace tt::umd {

SiliconTlbWindow::SiliconTlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config, IoSafety io_safety) :
    TlbWindow(std::move(handle), config, io_safety) {}

void SiliconTlbWindow::write16(uint64_t offset, uint16_t value) { UMD_NOT_IMPLEMENTED(); }

uint16_t SiliconTlbWindow::read16(uint64_t offset) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::write32(uint64_t offset, uint32_t value) { UMD_NOT_IMPLEMENTED(); }

uint32_t SiliconTlbWindow::read32(uint64_t offset) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::write64(uint64_t offset, uint64_t value) { UMD_NOT_IMPLEMENTED(); }

uint64_t SiliconTlbWindow::read64(uint64_t offset) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::write_register(uint64_t offset, const void* data, size_t size) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::read_register(uint64_t offset, void* data, size_t size) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::write_block(uint64_t offset, const void* data, size_t size) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::read_block(uint64_t offset, void* data, size_t size) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::set_io_timeout_hang_check(const std::function<bool(NocId)>& hang_check) {
    UMD_NOT_IMPLEMENTED();
}

void SiliconTlbWindow::configure(const tlb_data& new_config) { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbWindow::set_sigbus_safe_handler(bool set_safe_handler) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
