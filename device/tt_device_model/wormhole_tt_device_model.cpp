// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/wormhole_tt_device_model.hpp"

namespace tt::umd {

WormholeTTDeviceModel::WormholeTTDeviceModel(int communication_device_id) :
    communication_device_id_(communication_device_id) {}

tt::ARCH WormholeTTDeviceModel::get_arch() const { return tt::ARCH::WORMHOLE_B0; }

int WormholeTTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

}  // namespace tt::umd
