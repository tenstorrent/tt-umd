// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/arc/spi_tt_device.hpp"

#include <stdexcept>

#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

SPITTDevice::SPITTDevice(TTDevice *device) : device_(device) {
    if (device_ == nullptr) {
        throw std::runtime_error("SPITTDevice: device pointer cannot be null");
    }
    // TODO: Implement architecture-specific SPI operations based on device->get_arch()
    // For now, SPI operations will throw runtime_error until implementations are added.
}

void SPITTDevice::read(uint32_t addr, uint8_t *data, size_t size) {
    // TODO: Implement architecture-specific SPI read based on device_->get_arch().
    throw std::runtime_error("SPI read not yet implemented for this device architecture.");
}

void SPITTDevice::write(uint32_t addr, const uint8_t *data, size_t size, bool skip_write_to_spi) {
    // TODO: Implement architecture-specific SPI write based on device_->get_arch().
    throw std::runtime_error("SPI write not yet implemented for this device architecture.");
}

}  // namespace tt::umd
