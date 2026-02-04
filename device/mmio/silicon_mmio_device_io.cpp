// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/mmio/silicon_mmio_device_io.hpp"

#include <stdexcept>

namespace tt::umd {

SiliconMMIODeviceIO::SiliconMMIODeviceIO(
    tt_device_t* tt_device,
    size_t size,
    const TlbMapping tlb_mapping,
    const tlb_data& config)
    : tlb_window_(std::make_unique<TlbWindow>(
          std::make_unique<TlbHandle>(tt_device, size, tlb_mapping), config)) {}

void SiliconMMIODeviceIO::write32(uint64_t offset, uint32_t value) {
    tlb_window_->write32(offset, value);
}

uint32_t SiliconMMIODeviceIO::read32(uint64_t offset) {
    return tlb_window_->read32(offset);
}

void SiliconMMIODeviceIO::write_register(uint64_t offset, const void* data, size_t size) {
    tlb_window_->write_register(offset, data, size);
}

void SiliconMMIODeviceIO::read_register(uint64_t offset, void* data, size_t size) {
    tlb_window_->read_register(offset, data, size);
}

void SiliconMMIODeviceIO::write_block(uint64_t offset, const void* data, size_t size) {
    tlb_window_->write_block(offset, data, size);
}

void SiliconMMIODeviceIO::read_block(uint64_t offset, void* data, size_t size) {
    tlb_window_->read_block(offset, data, size);
}

void SiliconMMIODeviceIO::read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    tlb_window_->read_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void SiliconMMIODeviceIO::write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    tlb_window_->write_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void SiliconMMIODeviceIO::noc_multicast_write_reconfigure(
    void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    tlb_window_->noc_multicast_write_reconfigure(dst, size, core_start, core_end, addr, ordering);
}

size_t SiliconMMIODeviceIO::get_size() const {
    return tlb_window_->get_size();
}

void SiliconMMIODeviceIO::configure(const tlb_data& new_config) {
    tlb_window_->configure(new_config);
}

uint64_t SiliconMMIODeviceIO::get_base_address() const {
    return tlb_window_->get_base_address();
}

TlbWindow* SiliconMMIODeviceIO::get_tlb_window() const {
    return tlb_window_.get();
}

void SiliconMMIODeviceIO::validate(uint64_t offset, size_t size) const {
    if (offset + size > get_size()) {
        throw std::runtime_error("SiliconMMIODeviceIO: Access beyond window boundary");
    }
}

}  // namespace tt::umd