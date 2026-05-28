// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include "tt_architecture_implementation_doxy.hpp"
#include "tt_device_firmware_doxy.hpp"
#include "tt_device_protocol_doxy.hpp"
#include "tt_firmware_info_provider_doxy.hpp"
#include "tt_firmware_telemetry_reader_doxy.hpp"
#include "tt_hang_detector_doxy.hpp"

namespace tt::umd {

class TTDeviceModel {
public:
    virtual ~TTDeviceModel() = default;

    virtual DeviceProtocol* get_device_protocol() = 0;
    virtual DeviceFirmware* get_device_firmware() = 0;
    virtual ArchitectureImplementation* get_architecture_impl() = 0;
    virtual std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) = 0;

    virtual HangDetector* get_hang_detector() { return nullptr; }

    virtual FirmwareTelemetryReader* get_firmware_telemetry_reader() { return nullptr; }

    virtual FirmwareInfoProvider* get_firmware_info_provider() { return nullptr; }
};

}  // namespace tt::umd
