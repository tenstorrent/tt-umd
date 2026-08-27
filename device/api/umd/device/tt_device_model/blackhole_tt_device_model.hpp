// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

// Model for a Blackhole device, over any transport that reaches one.
class BlackholeTTDeviceModel : public TTDeviceModel {
public:
    explicit BlackholeTTDeviceModel(int communication_device_id);

    tt::ARCH get_arch() const override;

    int get_communication_device_id() const override;

private:
    int communication_device_id_;
};

}  // namespace tt::umd
