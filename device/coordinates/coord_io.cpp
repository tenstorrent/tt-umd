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

}  // namespace tt::umd
