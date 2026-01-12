// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include <unordered_map>

namespace tt::umd {

enum class IODeviceType {
    PCIe,
    JTAG,
    UNKNOWN,
};

// Const map of Device type names for each of the types listed in the enum.
static const std::unordered_map<IODeviceType, std::string> DeviceTypeToString = {
    {IODeviceType::PCIe, "PCIe"},
    {IODeviceType::JTAG, "JTAG"},
    {IODeviceType::UNKNOWN, "Unknown"},
};

}  // namespace tt::umd
