// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "io_window/kmd_noc_window.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

KmdNocWindow::KmdNocWindow(PCIDevice* pci_device, const TargetIoWindowConfig& target, const HostIoWindowConfig& host) :
    pci_device_(pci_device), target_(target), host_(host) {}

TargetIoWindowConfig KmdNocWindow::get_target_config() const { return target_; }

IoOrdering KmdNocWindow::get_io_ordering() const { return ordering_; }

size_t KmdNocWindow::get_size() const { return host_.size; }

HostMemoryCaching KmdNocWindow::get_memory_caching_type() const { return host_.mapping; }

void KmdNocWindow::write_block(uint64_t offset, const void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::write_block is not implemented yet");
}

void KmdNocWindow::read_block(uint64_t offset, void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::read_block is not implemented yet");
}

void KmdNocWindow::write16(uint64_t offset, uint16_t value) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::write16 is not implemented yet");
}

uint16_t KmdNocWindow::read16(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::read16 is not implemented yet");
}

void KmdNocWindow::write32(uint64_t offset, uint32_t value) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::write32 is not implemented yet");
}

uint32_t KmdNocWindow::read32(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::read32 is not implemented yet");
}

void KmdNocWindow::write64(uint64_t offset, uint64_t value) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::write64 is not implemented yet");
}

uint64_t KmdNocWindow::read64(uint64_t offset) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::read64 is not implemented yet");
}

void KmdNocWindow::write_aligned(uint64_t offset, const void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::write_aligned is not implemented yet");
}

void KmdNocWindow::read_aligned(uint64_t offset, void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::read_aligned is not implemented yet");
}

void KmdNocWindow::configure(const TargetIoWindowConfig& config) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::configure is not implemented yet");
}

void KmdNocWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) {
    UMD_THROW(error::RuntimeError, "KmdNocWindow::configure is not implemented yet");
}

}  // namespace tt::umd
