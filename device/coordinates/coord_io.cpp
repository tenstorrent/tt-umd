// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/coordinates/coord_io.hpp"

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

CoordIO::CoordIO(TTDevice *tt_device, const SocDescriptor &soc_descriptor) :
    tt_device_(tt_device), soc_descriptor_(soc_descriptor) {}

void CoordIO::read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, uint32_t size) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(core);
    tt_device_->read_from_device(mem_ptr, translated_core, addr, size);
}

void CoordIO::write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, uint32_t size) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(core);
    tt_device_->write_to_device(mem_ptr, translated_core, addr, size);
}

void CoordIO::noc_multicast_write(void *dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    tt_xy_pair translated_core_start = soc_descriptor_.translate_chip_coord_to_translated(core_start);
    tt_xy_pair translated_core_end = soc_descriptor_.translate_chip_coord_to_translated(core_end);
    tt_device_->noc_multicast_write(dst, size, translated_core_start, translated_core_end, addr);
}

std::chrono::milliseconds CoordIO::wait_eth_core_training(
    CoreCoord eth_core, const std::chrono::milliseconds timeout_ms) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(eth_core);
    return tt_device_->wait_eth_core_training(translated_core, timeout_ms);
}

uint32_t CoordIO::get_risc_reset_state(CoreCoord core) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(core);
    return tt_device_->get_risc_reset_state(translated_core);
}

void CoordIO::set_risc_reset_state(CoreCoord core, const uint32_t risc_flags) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(core);
    tt_device_->set_risc_reset_state(translated_core, risc_flags);
}

void CoordIO::dma_write_to_device(const void *src, size_t size, CoreCoord core, uint64_t addr) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(core);
    tt_device_->dma_write_to_device(src, size, translated_core, addr);
}

void CoordIO::dma_read_from_device(void *dst, size_t size, CoreCoord core, uint64_t addr) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(core);
    tt_device_->dma_read_from_device(dst, size, translated_core, addr);
}

void CoordIO::dma_multicast_write(void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    tt_xy_pair translated_core_start = soc_descriptor_.translate_chip_coord_to_translated(core_start);
    tt_xy_pair translated_core_end = soc_descriptor_.translate_chip_coord_to_translated(core_end);
    tt_device_->dma_multicast_write(src, size, translated_core_start, translated_core_end, addr);
}

EthTrainingStatus CoordIO::read_eth_core_training_status(CoreCoord eth_core) {
    tt_xy_pair translated_core = soc_descriptor_.translate_chip_coord_to_translated(eth_core);
    return tt_device_->read_eth_core_training_status(translated_core);
}

}  // namespace tt::umd
