// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "architecture_implementation.hpp"
#include "device_firmware.hpp"
#include "device_info.hpp"
#include "device_protocol.hpp"
#include "firmware_info_provider.hpp"
#include "firmware_telemetry_reader.hpp"
#include "hang_detector.hpp"

namespace tt::umd {

class TTDeviceModel {
public:
    virtual ~TTDeviceModel() = default;

    virtual std::unique_ptr<DeviceInfo> create_device_info() = 0;

    virtual std::unique_ptr<DeviceProtocol> create_device_protocol() = 0;
    virtual std::unique_ptr<DeviceFirmware> create_device_firmware() = 0;
    virtual std::unique_ptr<ArchitectureImplementation> create_architecture_impl() = 0;

    virtual std::unique_ptr<HangDetector> create_hang_detector() { return nullptr; }

    virtual std::unique_ptr<FirmwareTelemetryReader> create_firmware_telemetry_reader() { return nullptr; }

    virtual std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider() { return nullptr; }
};

}  // namespace tt::umd
