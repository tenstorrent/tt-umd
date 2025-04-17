/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/wormhole_arc_messenger.h"

#include "logger.hpp"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/wormhole_implementation.h"

namespace tt::umd {

WormholeArcMessenger::WormholeArcMessenger(TTDevice* tt_device) : ArcMessenger(tt_device) {}

uint32_t WormholeArcMessenger::send_message(
    const uint32_t msg_code, std::vector<uint32_t>& return_values, uint16_t arg0, uint16_t arg1, uint32_t timeout_ms) {
    if ((msg_code & 0xff00) != wormhole::ARC_MSG_COMMON_PREFIX) {
        log_error("Malformed message. msg_code is 0x{:x} but should be 0xaa..", msg_code);
    }

    log_assert(arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args");

    auto lock = lock_manager.acquire_mutex(MutexType::ARC_MSG, tt_device->get_pci_device()->get_device_num());

    auto architecture_implementation = tt_device->get_architecture_implementation();

    uint32_t fw_arg = arg0 | (arg1 << 16);
    int exit_code = 0;

    tt_device->bar_write32(
        architecture_implementation->get_arc_reset_scratch_offset() +
            wormhole::ARC_SCRATCH_RES0_OFFSET * sizeof(uint32_t),
        fw_arg);
    tt_device->bar_write32(
        architecture_implementation->get_arc_reset_scratch_offset() +
            wormhole::ARC_SCRATCH_STATUS_OFFSET * sizeof(uint32_t),
        msg_code);

    uint32_t misc = tt_device->bar_read32(architecture_implementation->get_arc_reset_arc_misc_cntl_offset());
    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", 0);
        return 1;
    } else {
        tt_device->bar_write32(architecture_implementation->get_arc_reset_arc_misc_cntl_offset(), misc | (1 << 16));
    }

    uint32_t status = 0xbadbad;
    auto start = std::chrono::system_clock::now();
    while (true) {
        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms && timeout_ms != 0) {
            throw std::runtime_error(fmt::format("Timed out after waiting {} ms for ARC to respond", timeout_ms));
        }

        status = tt_device->bar_read32(
            architecture_implementation->get_arc_reset_scratch_offset() +
            wormhole::ARC_SCRATCH_STATUS_OFFSET * sizeof(uint32_t));

        if ((status & 0xffff) == (msg_code & 0xff)) {
            if (return_values.size() >= 1) {
                return_values[0] = tt_device->bar_read32(
                    architecture_implementation->get_arc_reset_scratch_offset() +
                    wormhole::ARC_SCRATCH_RES0_OFFSET * sizeof(uint32_t));
            }

            if (return_values.size() >= 2) {
                return_values[1] = tt_device->bar_read32(
                    architecture_implementation->get_arc_reset_scratch_offset() +
                    wormhole::ARC_SCRATCH_RES1_OFFSET * sizeof(uint32_t));
            }

            exit_code = (status & 0xffff0000) >> 16;
            break;
        } else if (status == HANG_READ_VALUE) {
            log_warning(LogSiliconDriver, "On device {}, message code 0x{:x} not recognized by FW", 0, msg_code);
            exit_code = HANG_READ_VALUE;
            break;
        }
    }

    tt_device->detect_hang_read();
    return exit_code;
}

}  // namespace tt::umd
