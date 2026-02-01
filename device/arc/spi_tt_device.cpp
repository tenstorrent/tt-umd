// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/arc/spi_tt_device.hpp"

#include <fmt/format.h>

#include <memory>
#include <stdexcept>

#include "umd/device/arc/wormhole_spi_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

std::unique_ptr<SPITTDevice> SPITTDevice::create(TTDevice *device) {
    if (device == nullptr) {
        throw std::runtime_error("SPITTDevice: device pointer cannot be null");
    }

    switch (device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeSPITTDevice>(device);
        default:
            throw std::runtime_error(
                fmt::format("SPI operations are not supported for {} architecture.", device->get_arch()));
    }
}

SPITTDevice::SPITTDevice(TTDevice *device) : device_(device) {
    if (device_ == nullptr) {
        throw std::runtime_error("SPITTDevice: device pointer cannot be null");
    }
}

}  // namespace tt::umd
