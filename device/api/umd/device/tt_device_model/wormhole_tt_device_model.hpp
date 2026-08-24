// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

// Model for a Wormhole device, over any transport that reaches one.
class WormholeTTDeviceModel : public TTDeviceModel {
public:
    WormholeTTDeviceModel(IODeviceType communication_device_type, int communication_device_id);

    tt::ARCH get_arch() const override;

    IODeviceType get_communication_device_type() const override;

    int get_communication_device_id() const override;

private:
    IODeviceType communication_device_type_;
    int communication_device_id_;
};

}  // namespace tt::umd
