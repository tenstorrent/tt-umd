// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/wormhole_tt_device.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "tracy.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector.hpp"
#include "umd/device/tt_device/hang_detection/wormhole_hang_detector.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/wormhole_eth.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

WormholeTTDevice::WormholeTTDevice(std::unique_ptr<TTDeviceModel> model) : TTDevice(std::move(model)) {}

uint32_t WormholeTTDevice::get_clock() {
    // There is one return value from AICLK ARC message.
    DeviceCommandResult result = get_device_firmware()->send_device_command(
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::GET_AICLK),
        {0xFFFF, 0xFFFF},
        timeout::ARC_MESSAGE_TIMEOUT,
        get_selected_noc_id());
    if (result.exit_code != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to get AICLK value with exit code: {}", result.exit_code));
    }
    return result.return_values.at(0);
}

uint32_t WormholeTTDevice::get_min_clock_freq() { return get_architecture_implementation()->get_min_clock_freq(); }

}  // namespace tt::umd
