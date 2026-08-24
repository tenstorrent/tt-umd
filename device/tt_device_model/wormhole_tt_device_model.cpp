// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/wormhole_tt_device_model.hpp"

namespace tt::umd {

WormholeTTDeviceModel::WormholeTTDeviceModel(IODeviceType communication_device_type, int communication_device_id) :
    communication_device_type_(communication_device_type), communication_device_id_(communication_device_id) {}

tt::ARCH WormholeTTDeviceModel::get_arch() const { return tt::ARCH::WORMHOLE_B0; }

IODeviceType WormholeTTDeviceModel::get_communication_device_type() const { return communication_device_type_; }

int WormholeTTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

}  // namespace tt::umd
