// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdexcept>
#include <vector>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

template <typename DATA_T>
class UMDException {
public:
    UMDException(const std::string& what, const DATA_T& data) : what_string_(what), exception_data_(data) {}

    std::string& what() { return what_string_; }

    const std::string& what() const noexcept { return what_string_; }

    DATA_T& data() { return exception_data_; }

    const DATA_T& data() const noexcept { return exception_data_; }

private:
    std::string what_string_;
    DATA_T exception_data_;
    // TODO: Try adding stack trace and source line information.
};

struct CoreExceptionData {
public:
    xy_pair core;
};

struct ETHHeartbeatFailureData : public CoreExceptionData {
    uint32_t postcode;
    uint32_t heartbeat_value;
};

using CoreException = UMDException<CoreExceptionData>;
using ETHHeartbeatException = UMDException<ETHHeartbeatFailureData>;

}  // namespace tt::umd
