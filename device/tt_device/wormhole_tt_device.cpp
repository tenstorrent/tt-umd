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
#include "umd/device/arc/arc_messenger.hpp"
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

WormholeTTDevice::WormholeTTDevice(std::unique_ptr<TTDeviceModel> model) : TTDevice(std::move(model)) {
    WormholeTTDevice::set_arc_coordinate();
}

bool WormholeTTDevice::get_noc_translation_enabled() {
    uint32_t niu_cfg = 0x0;
    constexpr uint32_t ARC_APB_NIU_0_OFFSET = 0x50000;
    constexpr uint32_t NIU_CFG_0_OFFSET = 0x100;
    read_from_arc_apb(&niu_cfg, ARC_APB_NIU_0_OFFSET + NIU_CFG_0_OFFSET, sizeof niu_cfg);
    return (niu_cfg & (1 << 14)) != 0;
}

ChipInfo WormholeTTDevice::get_chip_info() {
    ChipInfo chip_info = TTDevice::get_chip_info();

    DeviceCommandResult result = get_device_firmware()->send_device_command(
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::ARC_GET_HARVESTING),
        {0, 0},
        timeout::ARC_MESSAGE_TIMEOUT,
        get_selected_noc_id());

    if (result.exit_code != 0) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Failed to get harvesting masks with exit code: {}", result.exit_code));
    }

    chip_info.harvesting_masks.tensix_harvesting_mask =
        CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, result.return_values.at(0));

    return chip_info;
}

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

uint32_t WormholeTTDevice::get_power_state_arc_msg(TTDevice::PowerState state) {
    uint32_t msg = wormhole::ARC_MSG_COMMON_PREFIX;
    switch (state) {
        case TTDevice::PowerState::BUSY: {
            msg |= get_architecture_implementation()->get_firmware_message_go_busy();
            break;
        }
        case TTDevice::PowerState::IDLE: {
            msg |= get_architecture_implementation()->get_firmware_message_go_idle();
            break;
        }
        default:
            UMD_THROW(error::RuntimeError, "Unrecognized power state.");
    }
    return msg;
}

void WormholeTTDevice::set_clock_state(TTDevice::PowerState state, NocId /*noc_id*/) {
    ZoneScoped;
    uint32_t msg = get_power_state_arc_msg(state);
    DeviceCommandResult result =
        get_device_firmware()->send_device_command(msg, {0, 0}, timeout::ARC_MESSAGE_TIMEOUT, get_selected_noc_id());
    UMD_ASSERT(
        result.exit_code == 0,
        error::RuntimeError,
        fmt::format("Failed to set clock state to {} with exit code: {}", (int)state, result.exit_code));
    wait_for_aiclk_value(state);
}

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

void WormholeTTDevice::read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_APB_ADDRESS_RANGE) {
        UMD_THROW(error::RuntimeError, "Address is out of ARC APB address range.");
    }
    if (is_remote()) {
        read_from_device_reg(mem_ptr, get_arc_core(), registers_.arc_apb_noc_base_address + arc_addr_offset, size);
        return;
    }
    if (get_communication_device_type() == IODeviceType::JTAG) {
        get_device_protocol()->read_ctrl(
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0],
            registers_.arc_apb_noc_base_address + arc_addr_offset,
            sizeof(uint32_t),
            NocId::DEFAULT_NOC);
        return;
    }
    auto result = bar_read32(registers_.arc_apb_bar0_offset + arc_addr_offset);
    *(reinterpret_cast<uint32_t *>(mem_ptr)) = result;
}

void WormholeTTDevice::write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_APB_ADDRESS_RANGE) {
        UMD_THROW(error::RuntimeError, "Address is out of ARC APB address range.");
    }
    if (is_remote()) {
        write_to_device_reg(mem_ptr, get_arc_core(), registers_.arc_apb_noc_base_address + arc_addr_offset, size);
        return;
    }
    if (get_communication_device_type() == IODeviceType::JTAG) {
        get_device_protocol()->write_ctrl(
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0],
            registers_.arc_apb_noc_base_address + arc_addr_offset,
            sizeof(uint32_t),
            NocId::DEFAULT_NOC);
        return;
    }
    bar_write32(registers_.arc_apb_bar0_offset + arc_addr_offset, *(reinterpret_cast<const uint32_t *>(mem_ptr)));
}

std::chrono::milliseconds WormholeTTDevice::wait_eth_core_training(
    CoreCoord eth_core, const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    auto duration = std::chrono::milliseconds(0);

    auto start = std::chrono::steady_clock::now();
    while (read_eth_core_training_status(eth_core) == EthTrainingStatus::IN_PROGRESS) {
        auto end = std::chrono::steady_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration > timeout_ms) {
            if (get_board_type() != BoardType::UBB) {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format(
                        "ETH training timed out after {} ms, on eth core {}, {}",
                        timeout_ms.count(),
                        eth_core.x,
                        eth_core.y));
            } else {
                // We don't want to throw on 6u systems, but log a warning so it is visible.
                log_warning(
                    LogUMD,
                    "ETH training timed out after {} ms, on eth core {}, {}. Continuing for UBB board.",
                    timeout_ms.count(),
                    eth_core.x,
                    eth_core.y);
                break;
            }
        }
    }
    return duration;
}

EthTrainingStatus WormholeTTDevice::read_eth_core_training_status(CoreCoord eth_core) {
    uint32_t retrain_status;
    read_from_device_reg(&retrain_status, eth_core, wormhole::ETH_RETRAIN_ADDR, sizeof(uint32_t));
    // If core is in retrain state, then training status is not valid as the training is ongoing.
    // If the core is put in retrain state, we have to wait for the retrain state to clear before making sense out of
    // the training status.
    if (retrain_status == wormhole::ETH_TRIGGER_RETRAIN_VAL) {
        log_trace(LogUMD, "Core {} is in retrain state, training is ongoing.", eth_core.str());
        return EthTrainingStatus::IN_PROGRESS;
    }
    uint32_t training_status;
    read_from_device_reg(&training_status, eth_core, wormhole::ETH_TRAIN_STATUS_ADDR, sizeof(uint32_t));
    log_trace(LogUMD, "Training status for core {} is {}", eth_core.str(), training_status);

    if (training_status == static_cast<uint32_t>(EthTrainingStatus::FAIL)) {
        // Training can fail due to various reasons, but what we mostly care about is to detect whether this is
        // unconnected eth link or if the training truly failed on a connected eth link.
        uint32_t link_err_status;
        read_from_device_reg(&link_err_status, eth_core, wormhole::ETH_LINK_ERR_STATUS_ADDR, sizeof(uint32_t));
        log_trace(LogUMD, "Link error status for core {} is {}", eth_core.str(), link_err_status);
        if (link_err_status >= wormhole::ETH_LINK_UNUSED_ERROR_CODE_RANGE_START) {
            return EthTrainingStatus::NOT_CONNECTED;
        }
    }
    return static_cast<EthTrainingStatus>(training_status);
}

void WormholeTTDevice::retrain_dram_core(const uint32_t dram_channel) {
    UMD_THROW(error::RuntimeError, "DRAM retraining is not supported on WormholeTTDevice.");
}

void WormholeTTDevice::set_arc_coordinate() {
    arc_core_noc0 = wormhole::ARC_CORES_NOC0[0];
    arc_core_noc1 = tt_xy_pair(
        wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
        wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y]);
}

}  // namespace tt::umd
