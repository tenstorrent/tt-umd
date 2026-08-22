// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

TTDeviceModel::TTDeviceModel(tt::ARCH arch, IODeviceType communication_device_type, int communication_device_id) :
    arch_(arch),
    communication_device_type_(communication_device_type),
    communication_device_id_(communication_device_id) {}

tt::ARCH TTDeviceModel::get_arch() const { return arch_; }

IODeviceType TTDeviceModel::get_communication_device_type() const { return communication_device_type_; }

int TTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

}  // namespace tt::umd
