// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/wormhole_arc_messenger.hpp"

#include <chrono>
#include <tt-logger/tt-logger.hpp>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "assert.hpp"
#include "noc_access.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "utils.hpp"

namespace tt::umd {

WormholeArcMessenger::WormholeArcMessenger(TTDevice* tt_device) : ArcMessenger(tt_device) {}

uint32_t WormholeArcMessenger::send_message(
    const uint32_t msg_code,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms) {
    if ((msg_code & 0xff00) != wormhole::ARC_MSG_COMMON_PREFIX) {
        log_error(LogUMD, "Malformed message. msg_code is 0x{:x} but should be 0xaa..", msg_code);
    }

    // Validate that only 2 args are passed for Wormhole.
    if (args.size() > 2) {
        throw std::runtime_error(
            fmt::format("Wormhole ARC messenger only supports 2 arguments, but {} were provided", args.size()));
    }

    // Extract args (default to 0 if not provided).
    uint16_t arg0 = 0;
    uint16_t arg1 = 0;

    if (!args.empty()) {
        if (args[0] > 0xFFFF) {
            throw std::runtime_error(
                fmt::format("Argument 0 is 0x{:x}, which exceeds uint16_t maximum (0xFFFF) for Wormhole", args[0]));
        }
        arg0 = static_cast<uint16_t>(args[0]);
    }

    if (args.size() >= 2) {
        if (args[1] > 0xFFFF) {
            throw std::runtime_error(
                fmt::format("Argument 1 is 0x{:x}, which exceeds uint16_t maximum (0xFFFF) for Wormhole", args[1]));
        }
        arg1 = static_cast<uint16_t>(args[1]);
    }

    // TODO: Once local and remote ttdevice is properly separated, reenable this code.
    // TODO2: Once we have unique chip ids other than PCI dev number, use that for both local and remote chips for
    // locks.
    // It can happen that multiple topology discovery instances run in parallel, and they can create multiple
    // RemoteTTDevice objects over the same remote chip but using different local one. This will make the locks (which
    // are over pci device num) allow multiple remote arc messages to the same remote chip which will break the
    // communication. It can also happen that while topology discovery is running on a remote chip through one local
    // chip, regular cluster construction is running through another local chip. Currently there's no other solution
    // than to just lock all arc communication through the same lock. auto lock =
    //     tt_device->is_remote()
    //         ? lock_manager.acquire_mutex(MutexType::REMOTE_ARC_MSG, tt_device->get_pci_device()->get_device_num())
    //         : lock_manager.acquire_mutex(MutexType::ARC_MSG, tt_device->get_pci_device()->get_device_num());
    auto lock = lock_manager.acquire_mutex(MutexType::ARC_MSG);

    uint32_t fw_arg = arg0 | (arg1 << 16);
    int exit_code = 0;

    tt_device->write_to_arc_apb(&fw_arg, wormhole::ARC_RESET_SCRATCH_RES0_OFFSET, sizeof(uint32_t));
    tt_device->write_to_arc_apb(&msg_code, wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET, sizeof(uint32_t));

    tt_device->wait_for_non_mmio_flush();

    uint32_t misc;
    tt_device->read_from_arc_apb(&misc, wormhole::ARC_RESET_ARC_MISC_CNTL_OFFSET, sizeof(uint32_t));

    if (misc & (1 << 16)) {
        log_error(LogUMD, "trigger_fw_int failed on device {}", 0);
        return 1;
    } else {
        uint32_t val_wr = misc | (1 << 16);
        tt_device->write_to_arc_apb(&val_wr, wormhole::ARC_RESET_ARC_MISC_CNTL_OFFSET, sizeof(uint32_t));
    }

    uint32_t status = 0xbadbad;
    auto start = std::chrono::steady_clock::now();
    while (true) {
        tt_device->read_from_arc_apb(&status, wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET, sizeof(uint32_t));

        if ((status & 0xffff) == (msg_code & 0xff)) {
            if (!return_values.empty()) {
                tt_device->read_from_arc_apb(
                    return_values.data(), wormhole::ARC_RESET_SCRATCH_RES0_OFFSET, sizeof(uint32_t));
            }

            if (return_values.size() >= 2) {
                tt_device->read_from_arc_apb(
                    &return_values[1], wormhole::ARC_RESET_SCRATCH_RES1_OFFSET, sizeof(uint32_t));
            }

            exit_code = (status & 0xffff0000) >> 16;
            break;
        } else if (status == HANG_READ_VALUE) {
            log_warning(LogUMD, "On device {}, message code 0x{:x} not recognized by FW", 0, msg_code);
            exit_code = HANG_READ_VALUE;
            break;
        }

        utils::check_timeout(
            start, timeout_ms, fmt::format("Timed out after waiting {} ms for ARC to respond", timeout_ms));
    }

    tt_device->detect_hang_read();
    return exit_code;
}

}  // namespace tt::umd
