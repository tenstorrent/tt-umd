// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include <unordered_map>

namespace tt::umd {

// Explicit values, in the order the Base API spec lists the types, so adding one cannot silently renumber another.
enum class IODeviceType {
    PCIe = 0,
    JTAG = 1,
    AXI = 2,
    UNDEFINED = 3,
};

// Const map of Device type names for each of the types listed in the enum.
static const std::unordered_map<IODeviceType, std::string> DeviceTypeToString = {
    {IODeviceType::PCIe, "PCIe"},
    {IODeviceType::JTAG, "JTAG"},
    {IODeviceType::AXI, "AXI"},
    {IODeviceType::UNDEFINED, "Undefined"},
};

}  // namespace tt::umd
