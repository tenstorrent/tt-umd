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

void WormholeTTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    uint32_t dest_bar_lo = target & 0xffffffff;
    uint32_t dest_bar_hi = (target >> 32) & 0xffffffff;
    std::uint32_t region_id_to_use = region;

    // TODO: stop doing this.  It's related to HUGEPAGE_CHANNEL_3_SIZE_LIMIT.
    if (region == 3) {
        region_id_to_use = 4;  // Hack use region 4 for channel 3..this ensures that we have a smaller chan 3 address
                               // space with the correct start offset
    }

    if (get_communication_device_type() == IODeviceType::JTAG) {
        UMD_THROW(error::RuntimeError, "configure_iatu_region is redundant for JTAG communication type.");
    }

    bar_write32(registers_.arc_csm_bar0_mailbox_offset + 0 * 4, region_id_to_use);
    bar_write32(registers_.arc_csm_bar0_mailbox_offset + 1 * 4, dest_bar_lo);
    bar_write32(registers_.arc_csm_bar0_mailbox_offset + 2 * 4, dest_bar_hi);
    bar_write32(registers_.arc_csm_bar0_mailbox_offset + 3 * 4, region_size);
    get_device_firmware()->send_device_command(
        wormhole::ARC_MSG_COMMON_PREFIX |
            static_cast<uint32_t>(wormhole::arc_message_type::SETUP_IATU_FOR_PEER_TO_PEER),
        {0, 0},
        timeout::ARC_MESSAGE_TIMEOUT,
        get_selected_noc_id());

    // Print what just happened.
    uint32_t peer_region_start = region_id_to_use * region_size;
    uint32_t peer_region_end = (region_id_to_use + 1) * region_size - 1;
    log_debug(
        LogUMD,
        "    [region id {}] NOC to PCI address range 0x{:x}-0x{:x} mapped to addr 0x{:x}",
        region,
        peer_region_start,
        peer_region_end,
        target);
}

}  // namespace tt::umd
