// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"

namespace tt::umd {

BlackholeTTDeviceModel::BlackholeTTDeviceModel(int communication_device_id) :
    communication_device_id_(communication_device_id) {}

tt::ARCH BlackholeTTDeviceModel::get_arch() const { return tt::ARCH::BLACKHOLE; }

int BlackholeTTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

}  // namespace tt::umd
