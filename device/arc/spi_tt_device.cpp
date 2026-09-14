// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/arc/spi_tt_device.hpp"

#include <fmt/format.h>

#include <memory>
#include <string>

#include "umd/device/arc/blackhole_spi_tt_device.hpp"
#include "umd/device/arc/wormhole_spi_tt_device.hpp"
#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"
#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<SPITTDevice> SPITTDevice::create(TTDevice *device) {
    if (device == nullptr) {
        UMD_THROW(error::RuntimeError, "SPITTDevice: device pointer cannot be null.");
    }

    // The SPI flows reach the device through its protocol and its firmware component. Each arm below
    // has already committed to an architecture, so the device's firmware is known to be that
    // architecture's implementation and is narrowed here, keeping the SPI classes cast-free.
    DeviceProtocol *protocol = device->get_device_protocol();
    switch (device->get_arch()) {
        case tt::ARCH::BLACKHOLE: {
            auto *firmware = dynamic_cast<BlackholeDeviceFirmware *>(device->get_device_firmware());
            UMD_ASSERT(
                firmware != nullptr,
                error::RuntimeError,
                "SPI on Blackhole requires a device backed by BlackholeDeviceFirmware.");
            return std::make_unique<BlackholeSPITTDevice>(protocol, firmware);
        }
        case tt::ARCH::WORMHOLE_B0: {
            auto *firmware = dynamic_cast<WormholeDeviceFirmware *>(device->get_device_firmware());
            UMD_ASSERT(
                firmware != nullptr,
                error::RuntimeError,
                "SPI on Wormhole requires a device backed by WormholeDeviceFirmware.");
            return std::make_unique<WormholeSPITTDevice>(protocol, firmware);
        }
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("SPI operations are not supported for {} architecture.", arch_to_str(device->get_arch())));
    }
}

SPITTDevice::SPITTDevice(DeviceProtocol *protocol) : protocol_(protocol) {
    if (protocol_ == nullptr) {
        UMD_THROW(error::RuntimeError, "SPITTDevice: device protocol pointer cannot be null.");
    }
}

uint32_t SPITTDevice::get_spi_fw_bundle_version() {
    UMD_THROW(error::RuntimeError, "get_spi_fw_bundle_version is not supported for this architecture.");
}

}  // namespace tt::umd
