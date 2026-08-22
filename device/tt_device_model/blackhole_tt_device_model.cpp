// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"

namespace tt::umd {

BlackholeTTDeviceModel::BlackholeTTDeviceModel(IODeviceType communication_device_type, int communication_device_id) :
    TTDeviceModel(tt::ARCH::BLACKHOLE, communication_device_type, communication_device_id) {}

}  // namespace tt::umd
