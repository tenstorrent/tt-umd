// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/wormhole_tt_device.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "assert.hpp"
#include "noc_access.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/wormhole_eth.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "utils.hpp"

namespace tt::umd {

WormholeTTDevice::WormholeTTDevice(std::shared_ptr<PCIDevice> pci_device, bool use_safe_api) :
    TTDevice(std::move(pci_device), std::make_unique<wormhole_implementation>(), use_safe_api) {
    arc_core = is_selected_noc1() ? tt_xy_pair(
                                        wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                                        wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y])
                                  : wormhole::ARC_CORES_NOC0[0];
}

WormholeTTDevice::WormholeTTDevice(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id) :
    TTDevice(std::move(jtag_device), jlink_id, std::make_unique<wormhole_implementation>()) {
    arc_core = is_selected_noc1() ? tt_xy_pair(
                                        wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                                        wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y])
                                  : wormhole::ARC_CORES_NOC0[0];
}

WormholeTTDevice::WormholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication) :
    TTDevice(std::move(remote_communication), std::make_unique<wormhole_implementation>()) {
    arc_core = is_selected_noc1() ? tt_xy_pair(
                                        wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                                        wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y])
                                  : wormhole::ARC_CORES_NOC0[0];
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

    std::vector<uint32_t> arc_msg_return_values = {0};
    uint32_t ret_code = get_arc_messenger()->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
        arc_msg_return_values,
        {0, 0});

    if (ret_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get harvesting masks with exit code {}", ret_code));
    }

    chip_info.harvesting_masks.tensix_harvesting_mask =
        CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, arc_msg_return_values[0]);

    return chip_info;
}

uint32_t WormholeTTDevice::get_clock() {
    // There is one return value from AICLK ARC message.
    std::vector<uint32_t> arc_msg_return_values = {0};
    auto exit_code = get_arc_messenger()->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | get_architecture_implementation()->get_arc_message_get_aiclk(),
        arc_msg_return_values,
        {0xFFFF, 0xFFFF});
    if (exit_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get AICLK value with exit code {}", exit_code));
    }
    return arc_msg_return_values[0];
}

uint32_t WormholeTTDevice::get_min_clock_freq() { return wormhole::AICLK_IDLE_VAL; }

void WormholeTTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    uint32_t dest_bar_lo = target & 0xffffffff;
    uint32_t dest_bar_hi = (target >> 32) & 0xffffffff;
    std::uint32_t region_id_to_use = region;

    // TODO: stop doing this.  It's related to HUGEPAGE_CHANNEL_3_SIZE_LIMIT.
    if (region == 3) {
        region_id_to_use = 4;  // Hack use region 4 for channel 3..this ensures that we have a smaller chan 3 address
                               // space with the correct start offset
    }

    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("configure_iatu_region is redundant for JTAG communication type.");
    }

    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 0 * 4, region_id_to_use);
    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 1 * 4, dest_bar_lo);
    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 2 * 4, dest_bar_hi);
    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 3 * 4, region_size);
    arc_messenger_->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | architecture_impl_->get_arc_message_setup_iatu_for_peer_to_peer(), {0, 0});

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
        throw std::runtime_error("Address is out of ARC APB address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    if (is_remote()) {
        read_from_device(mem_ptr, arc_core, architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
        return;
    }
    auto result = bar_read32(wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset);
    *(reinterpret_cast<uint32_t *>(mem_ptr)) = result;
}

void WormholeTTDevice::write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_APB_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC APB address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    if (is_remote()) {
        write_to_device(mem_ptr, arc_core, architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
        return;
    }
    bar_write32(
        wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset, *(reinterpret_cast<const uint32_t *>(mem_ptr)));
}

void WormholeTTDevice::read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC CSM address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    if (is_remote()) {
        read_from_device(mem_ptr, arc_core, architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset, size);
        return;
    }
    auto result = bar_read32(wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START + arc_addr_offset);
    *(reinterpret_cast<uint32_t *>(mem_ptr)) = result;
}

void WormholeTTDevice::write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC CSM address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    if (is_remote()) {
        write_to_device(mem_ptr, arc_core, architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset, size);
        return;
    }
    bar_write32(
        wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START + arc_addr_offset, *(reinterpret_cast<const uint32_t *>(mem_ptr)));
}

std::chrono::milliseconds WormholeTTDevice::wait_eth_core_training(
    const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms) {
    constexpr uint64_t eth_core_heartbeat_addr = 0x1C;
    auto time_taken_heartbeat = std::chrono::milliseconds(0);
    auto time_taken_port = std::chrono::milliseconds(0);
    auto start = std::chrono::steady_clock::now();
    uint32_t heartbeat_val;

    tt_xy_pair actual_eth_core = eth_core;
    if (is_selected_noc1()) {
        actual_eth_core = tt_xy_pair(wormhole::NOC0_X_TO_NOC1_X[eth_core.x], wormhole::NOC0_Y_TO_NOC1_Y[eth_core.y]);
    }

    read_from_device(&heartbeat_val, actual_eth_core, eth_core_heartbeat_addr, sizeof(heartbeat_val));

    uint32_t new_heartbeat_val = heartbeat_val;
    while (new_heartbeat_val != heartbeat_val) {
        read_from_device(&new_heartbeat_val, actual_eth_core, eth_core_heartbeat_addr, sizeof(heartbeat_val));
        utils::check_timeout(start, timeout_ms, fmt::format("ETH training timed out after {} ms", timeout_ms));
    }

    start = std::chrono::steady_clock::now();
    while (read_eth_core_training_status(actual_eth_core) == EthTrainingStatus::IN_PROGRESS) {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        time_taken_port = duration;
        if (time_taken_port > timeout_ms) {
            if (get_board_type() != BoardType::UBB) {
                throw std::runtime_error(fmt::format(
                    "ETH training timed out after {} ms, on eth core {}, {}",
                    timeout_ms.count(),
                    actual_eth_core.x,
                    actual_eth_core.y));
            } else {
                // We don't want to throw on 6u systems, but log a warning so it is visible.
                log_warning(
                    LogUMD,
                    "ETH training timed out after {} ms, on eth core {}, {}. Continuing for UBB board.",
                    timeout_ms.count(),
                    actual_eth_core.x,
                    actual_eth_core.y);
                break;
            }
        }
    }
    return time_taken_heartbeat + time_taken_port;
}

EthTrainingStatus WormholeTTDevice::read_eth_core_training_status(tt_xy_pair eth_core) {
    uint32_t retrain_status;
    read_from_device(&retrain_status, eth_core, wormhole::ETH_RETRAIN_ADDR, sizeof(uint32_t));
    // If core is in retrain state, then training status is not valid as the training is ongoing.
    // If the core is put in retrain state, we have to wait for the retrain state to clear before making sense out of
    // the training status.
    if (retrain_status == wormhole::ETH_TRIGGER_RETRAIN_VAL) {
        log_trace(LogUMD, "Core {} is in retrain state, training is ongoing.", eth_core.str());
        return EthTrainingStatus::IN_PROGRESS;
    }
    uint32_t training_status;
    read_from_device(&training_status, eth_core, wormhole::ETH_TRAIN_STATUS_ADDR, sizeof(uint32_t));
    log_trace(LogUMD, "Training status for core {} is {}", eth_core.str(), training_status);

    if (training_status == static_cast<uint32_t>(EthTrainingStatus::FAIL)) {
        // Training can fail due to various reasons, but what we mostly care about is to detect whether this is
        // unconnected eth link or if the training truly failed on a connected eth link.
        uint32_t link_err_status;
        read_from_device(&link_err_status, eth_core, wormhole::ETH_LINK_ERR_STATUS_ADDR, sizeof(uint32_t));
        log_trace(LogUMD, "Link error status for core {} is {}", eth_core.str(), link_err_status);
        if (link_err_status >= wormhole::ETH_LINK_UNUSED_ERROR_CODE_RANGE_START) {
            return EthTrainingStatus::NOT_CONNECTED;
        }
    }
    return static_cast<EthTrainingStatus>(training_status);
}

bool WormholeTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) noexcept {
    // Status codes.
    constexpr uint32_t STATUS_NO_ACCESS = 0xFFFFFFFF;
    constexpr uint32_t STATUS_WATCHDOG_TRIGGERED = 0xDEADC0DE;
    constexpr uint32_t STATUS_BOOT_INCOMPLETE_1 = 0x00000060;
    constexpr uint32_t STATUS_BOOT_INCOMPLETE_2 = 0x11110000;
    constexpr uint32_t STATUS_ASLEEP_1 = 0x0000AA00;
    constexpr uint32_t STATUS_ASLEEP_2 = 0x55;
    constexpr uint32_t STATUS_INIT_DONE_1 = 0x00000001;
    constexpr uint32_t STATUS_INIT_DONE_2 = 0xFFFFDEAD;
    constexpr uint32_t STATUS_OLD_POST_CODE = 0;
    constexpr uint32_t STATUS_MESSAGE_QUEUED_MASK = 0xFFFFFF00;
    constexpr uint32_t STATUS_MESSAGE_QUEUED_VAL = 0x0000AA00;
    constexpr uint32_t STATUS_HANDLING_MESSAGE_MASK = 0xFF00FFFF;
    constexpr uint32_t STATUS_HANDLING_MESSAGE_VAL = 0xAA000000;
    constexpr uint32_t STATUS_MESSAGE_COMPLETE_MASK = 0x0000FFFF;
    constexpr uint32_t STATUS_MESSAGE_COMPLETE_MIN = 0x00000001;

    // Post codes.
    constexpr uint32_t POST_CODE_INIT_DONE = 0xC0DE0001;
    constexpr uint32_t POST_CODE_ARC_MSG_HANDLE_DONE = 0xC0DE003F;
    constexpr uint32_t POST_CODE_ARC_TIME_LAST = 0xC0DE007F;

    const auto start = std::chrono::steady_clock::now();
    constexpr auto spin_limit = std::chrono::microseconds(1000);
    while (true) {
        uint32_t bar_read_arc_reset_scratch_status;

        read_from_arc_apb(
            &bar_read_arc_reset_scratch_status,
            wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET,
            sizeof(bar_read_arc_reset_scratch_status));

        uint32_t bar_read_arc_post_code;

        read_from_arc_apb(
            &bar_read_arc_post_code,
            architecture_impl_->get_arc_reset_scratch_offset(),
            sizeof(bar_read_arc_post_code));

        uint32_t bar_read_arc_csm_pcie_dma_request;

        read_from_arc_csm(
            &bar_read_arc_csm_pcie_dma_request,
            wormhole::ARC_CSM_ARC_PCIE_DMA_REQUEST,
            sizeof(bar_read_arc_csm_pcie_dma_request));

        switch (bar_read_arc_reset_scratch_status) {
            case STATUS_NO_ACCESS:
                log_error(LogUMD, "NoAccess error");
                return false;
            case STATUS_WATCHDOG_TRIGGERED:
                log_error(LogUMD, "WatchdogTriggered error");
                return false;

            case STATUS_INIT_DONE_1:
            case STATUS_INIT_DONE_2:
                return true;

            case STATUS_OLD_POST_CODE: {
                bool pc_idle = (bar_read_arc_post_code == POST_CODE_INIT_DONE) ||
                               (bar_read_arc_post_code >= POST_CODE_ARC_MSG_HANDLE_DONE &&
                                bar_read_arc_post_code <= POST_CODE_ARC_TIME_LAST);
                if (pc_idle) {
                    return true;
                }
                break;
            }
            case STATUS_BOOT_INCOMPLETE_1:
            case STATUS_BOOT_INCOMPLETE_2:
            case STATUS_ASLEEP_1:
            case STATUS_ASLEEP_2:
            default:
                break;
        }

        uint32_t message_id = 0;
        bool is_queued =
            ((bar_read_arc_reset_scratch_status & STATUS_MESSAGE_QUEUED_MASK) == STATUS_MESSAGE_QUEUED_VAL);
        bool is_handling =
            ((bar_read_arc_reset_scratch_status & STATUS_HANDLING_MESSAGE_MASK) == STATUS_HANDLING_MESSAGE_VAL);
        bool is_complete =
            ((bar_read_arc_reset_scratch_status & STATUS_MESSAGE_COMPLETE_MASK) > STATUS_MESSAGE_COMPLETE_MIN);
        bool dma_request = (bar_read_arc_csm_pcie_dma_request != 0);

        if (is_queued) {
            message_id = bar_read_arc_reset_scratch_status & 0xFF;
        } else if (is_handling) {
            message_id = (bar_read_arc_reset_scratch_status >> 16) & 0xFF;
        } else if (is_complete && !dma_request) {
            // We only return true if the message says complete and DMA is idle.
            return true;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;

        // If we are within the first 200us, busy-wait (continue).
        // This burns CPU, but guarantees we catch the status change instantly in this interval.
        if (elapsed < spin_limit) {
            // Optional: For 0ms timeouts, check manually here without strings.
            if (elapsed > timeout_ms) {
                return false;
            }
            continue;
        }

        if (utils::check_timeout(
                start,
                timeout_ms,
                fmt::format(
                    "Wait for ARC core to start timed out after: {}. Status: 0x{:x}, PostCode: 0x{:x}, MessageId "
                    "0x{:x}",
                    timeout_ms.count(),
                    arc_core.x,
                    arc_core.y,
                    bar_read_arc_reset_scratch_status,
                    bar_read_arc_post_code,
                    message_id),
                utils::TimeoutAction::Return)) {
            return false;
        }

        // If past 200us, avoid busy-waiting. Request a 10us sleep (minimum) -
        // actual duration will be longer due to OS scheduling and jitter.
        // This prevents 100% CPU usage during longer hardware initialization.
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

}  // namespace tt::umd
