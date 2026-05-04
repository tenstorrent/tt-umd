// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/arc/spi_tt_device.hpp"

#include <fmt/format.h>

#include <memory>
#include <string>

#include "umd/device/arc/blackhole_spi_tt_device.hpp"
#include "umd/device/arc/wormhole_spi_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<SPITTDevice> SPITTDevice::create(TTDevice *device) {
    if (device == nullptr) {
        UMD_THROW(error::RuntimeError, "SPITTDevice: device pointer cannot be null.");
    }

    switch (device->get_arch()) {
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeSPITTDevice>(device);
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeSPITTDevice>(device);
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("SPI operations are not supported for {} architecture.", arch_to_str(device->get_arch())));
    }
}

SPITTDevice::SPITTDevice(TTDevice *device) : device_(device) {
    if (device_ == nullptr) {
        UMD_THROW(error::RuntimeError, "SPITTDevice: device pointer cannot be null.");
    }
}

uint32_t SPITTDevice::get_spi_fw_bundle_version() {
    UMD_THROW(
        error::RuntimeError,
        fmt::format(
            "get_spi_fw_bundle_version is not supported for {} architecture.", arch_to_str(device_->get_arch())));
}

}  // namespace tt::umd
