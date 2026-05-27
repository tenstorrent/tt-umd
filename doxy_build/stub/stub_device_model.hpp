// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tt_device_model.hpp"

namespace tt::umd {

class StubDeviceModel : public TTDeviceModel {
public:
    std::unique_ptr<DeviceInfo> create_device_info() override;
    std::unique_ptr<DeviceProtocol> create_device_protocol() override;
    std::unique_ptr<DeviceFirmware> create_device_firmware() override;
    std::unique_ptr<ArchitectureImplementation> create_architecture_impl() override;
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) override;
    std::unique_ptr<DeviceController> create_device_controller() override;
};

}  // namespace tt::umd
