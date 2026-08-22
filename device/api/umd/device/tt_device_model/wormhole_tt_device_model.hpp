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
};

}  // namespace tt::umd
