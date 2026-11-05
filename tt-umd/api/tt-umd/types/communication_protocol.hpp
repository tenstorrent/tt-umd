// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include <unordered_map>

namespace tt::umd {

// The values are part of the ABI: keep them, and give a new type the next free one.
enum class IODeviceType {
    PCIe = 0,
    JTAG = 1,
    UNDEFINED = 2,
    AXI = 3,
};

// Const map of Device type names for each of the types listed in the enum.
static const std::unordered_map<IODeviceType, std::string> DeviceTypeToString = {
    {IODeviceType::PCIe, "PCIe"},
    {IODeviceType::JTAG, "JTAG"},
    {IODeviceType::AXI, "AXI"},
    {IODeviceType::UNDEFINED, "Undefined"},
};

}  // namespace tt::umd
