// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/wormhole_tt_device_model.hpp"

namespace tt::umd {

WormholeTTDeviceModel::WormholeTTDeviceModel(IODeviceType communication_device_type, int communication_device_id) :
    TTDeviceModel(tt::ARCH::WORMHOLE_B0, communication_device_type, communication_device_id) {}

}  // namespace tt::umd
