// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"

#include <fmt/format.h>

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/firmware_telemetry_reader.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/architecture_registers.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_info_provider_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_eth.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/timeouts.hpp"
#include "utils.hpp"

namespace tt::umd {

// How this class picks a route for ARC accesses: a non-null RemoteInterface means the device is
// reached over ethernet through a gateway, a non-null JtagInterface means it is reached over JTAG,
// and otherwise it is reached over PCIe. Inferring the route from which optional interface is
// present is sound because a TTDevice is built for exactly one communication protocol. The routing
// itself lives in WormholeArcWindow.

WormholeDeviceFirmware::WormholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    remote_interface_(remote_interface),
    architecture_impl_(architecture_impl),
    arc_apb_(WormholeArcWindow::arc_apb(device_protocol, pcie_interface, jtag_interface, remote_interface)),
    arc_csm_(WormholeArcWindow::arc_csm(device_protocol, pcie_interface, jtag_interface, remote_interface)) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "WormholeDeviceFirmware requires a DeviceProtocol.");
    UMD_ASSERT(
        architecture_impl_ != nullptr,
        error::RuntimeError,
        "WormholeDeviceFirmware requires an ArchitectureImplementation.");
    const int transports = (pcie_interface_ != nullptr) + (jtag_interface_ != nullptr) + (remote_interface_ != nullptr);
    UMD_ASSERT(
        transports == 1,
        error::RuntimeError,
        "WormholeDeviceFirmware requires exactly one of a PcieInterface, a JtagInterface or a RemoteInterface, since "
        "which one is present is how it picks the route for an access.");

    // Read after the checks above, not in the member initialiser list: that runs first, so a null
    // protocol faulted there before the assert could report it.
    device_id_ = device_protocol_->get_mmio_id();

    // Wormhole serializes all ARC traffic on one system-wide mutex rather than a per-device one:
    // several topology discovery instances can reach the same remote chip through different local
    // chips, so a per-device lock would let concurrent messages interleave on that chip. This mirrors
    // WormholeArcMessenger::send_message and the TODO recorded there.
    LockManager::initialize_mutex(MutexType::ARC_MSG);

    // The ARC core is at a fixed NOC0 coordinate on Wormhole, so both coordinates are known without
    // reading anything from the device.
    arc_core_noc0_ = wormhole::ARC_CORES_NOC0[0];
    arc_core_noc1_ = tt_xy_pair(
        wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
        wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y]);
}

WormholeDeviceFirmware::~WormholeDeviceFirmware() = default;

IODeviceType WormholeDeviceFirmware::get_io_device_type() const {
    return jtag_interface_ != nullptr ? IODeviceType::JTAG : IODeviceType::PCIe;
}

void WormholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // TODO: temporary. init_firmware() does two jobs - waiting for the firmware and building what
    // depends on it - so callers that only need the first (warm reset, for one) still run the
    // second, and a rebuild here would discard a live set. The intended fix is to split the two
    // into separate API calls, at which point this guard goes away.
    //
    // Safe today only because every caller creates the TTDevice after a reset rather than reusing
    // one across it, so there is never pre-reset state to preserve.
    if (firmware_info_provider_ != nullptr) {
        return;
    }

    wait_firmware_ready(timeout_ms, noc_id);

    // The telemetry reader and info provider read state the firmware publishes, so this is the
    // earliest point they can exist.
    firmware_telemetry_reader_ = ArcTelemetryReader::create_arc_telemetry_reader(
        device_protocol_, tt::ARCH::WORMHOLE_B0, arc_core_noc0_, arc_core_noc1_);

    firmware_info_provider_ = FirmwareInfoProviderImplementation::create_firmware_info_provider(
        tt::ARCH::WORMHOLE_B0, device_protocol_, arc_core_noc0_, arc_core_noc1_, firmware_telemetry_reader_.get());
}

FirmwareTelemetryReader* WormholeDeviceFirmware::get_firmware_telemetry_reader() const {
    return firmware_telemetry_reader_.get();
}

FirmwareInfoProvider* WormholeDeviceFirmware::get_firmware_info_provider() const {
    return firmware_info_provider_.get();
}

void WormholeDeviceFirmware::wait_firmware_ready(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // Deliberate difference from the deleted TTDevice wait: its JTAG reads were pinned to the NOC0
    // ARC coordinate over the default NOC whatever NOC the caller asked for. Every read here uses
    // noc_id consistently, coordinate and routing both; with the default NOC0 the two are
    // bit-identical on every transport.
    // One throwaway read before the poll, so a dead ARC APB path faults on an access that is
    // clearly a probe rather than partway into the boot-status loop. This was
    // TTDevice::probe_arc(), moved here with its only caller.
    uint32_t dummy;
    read_from_arc_apb(&dummy, wormhole::ARC_RESET_SCRATCH_OFFSET, sizeof(dummy), noc_id);

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

    uint32_t arc_reset_scratch_status = 0;
    uint32_t arc_post_code = 0;
    uint32_t message_id = 0;

    constexpr auto busy_poll_window = std::chrono::microseconds(1000);
    constexpr auto poll_interval = std::chrono::microseconds(10);

    const bool arc_core_started = utils::poll_until(
        [this, &arc_reset_scratch_status, &arc_post_code, &message_id, &noc_id]() {
            read_from_arc_apb(
                &arc_reset_scratch_status,
                wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET,
                sizeof(arc_reset_scratch_status),
                noc_id);

            read_from_arc_apb(&arc_post_code, wormhole::ARC_RESET_SCRATCH_OFFSET, sizeof(arc_post_code), noc_id);

            uint32_t arc_csm_pcie_dma_request = 0;
            read_from_arc_csm(
                &arc_csm_pcie_dma_request,
                wormhole::ARC_CSM_ARC_PCIE_DMA_REQUEST,
                sizeof(arc_csm_pcie_dma_request),
                noc_id);

            switch (arc_reset_scratch_status) {
                case STATUS_NO_ACCESS:
                case STATUS_WATCHDOG_TRIGGERED:
                    UMD_THROW(
                        error::FirmwareStartupError,
                        get_io_device_type(),
                        device_id_,
                        tt::ARCH::WORMHOLE_B0,
                        noc_id,
                        get_firmware_noc_coord(noc_id),
                        arc_reset_scratch_status,
                        arc_post_code);

                case STATUS_INIT_DONE_1:
                case STATUS_INIT_DONE_2:
                    return true;

                case STATUS_OLD_POST_CODE: {
                    const bool pc_idle =
                        (arc_post_code == POST_CODE_INIT_DONE) ||
                        (arc_post_code >= POST_CODE_ARC_MSG_HANDLE_DONE && arc_post_code <= POST_CODE_ARC_TIME_LAST);
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

            const bool is_queued =
                ((arc_reset_scratch_status & STATUS_MESSAGE_QUEUED_MASK) == STATUS_MESSAGE_QUEUED_VAL);
            const bool is_handling =
                ((arc_reset_scratch_status & STATUS_HANDLING_MESSAGE_MASK) == STATUS_HANDLING_MESSAGE_VAL);
            const bool is_complete =
                ((arc_reset_scratch_status & STATUS_MESSAGE_COMPLETE_MASK) > STATUS_MESSAGE_COMPLETE_MIN);
            const bool dma_request = (arc_csm_pcie_dma_request != 0);

            if (is_queued) {
                message_id = arc_reset_scratch_status & 0xFF;
            } else if (is_handling) {
                message_id = (arc_reset_scratch_status >> 16) & 0xFF;
            } else if (is_complete && !dma_request) {
                // We only return if the message says complete and DMA is idle.
                return true;
            }
            return false;
        },
        timeout_ms,
        busy_poll_window,
        poll_interval);

    if (!arc_core_started) {
        UMD_THROW(
            error::FirmwareStartupError,
            get_io_device_type(),
            device_id_,
            tt::ARCH::WORMHOLE_B0,
            noc_id,
            get_firmware_noc_coord(noc_id),
            arc_reset_scratch_status,
            arc_post_code,
            timeout_ms,
            message_id);
    }
}

DeviceCommandResult WormholeDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout, NocId noc_id) {
    // No commands before the firmware is up. Wormhole messages go through scratch registers that are
    // readable either way, so nothing stops the access -- it would just be talking to firmware that
    // has not reported ready.
    if (firmware_info_provider_ == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, get_io_device_type(), device_id_, tt::ARCH::WORMHOLE_B0);
    }

    if ((msg_code & 0xff00) != wormhole::ARC_MSG_COMMON_PREFIX) {
        log_error(LogUMD, "Malformed message. msg_code is {:#x} but should be 0xaa..", msg_code);
    }

    if (args.size() > 2) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Wormhole ARC messages are limited to 2 arguments, but {} were provided.", args.size()));
    }

    // The two 16-bit args are packed into a single 32-bit word (arg0 | arg1 << 16) sent to firmware.
    // The firmware treats the combined value 0xFFFFFFFF as a sentinel meaning "no argument provided",
    // triggering default behavior for messages that don't require arguments.
    uint16_t arg0 = 0xFFFF;
    uint16_t arg1 = 0xFFFF;

    if (!args.empty()) {
        if (args[0] > 0xFFFF) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Argument 0 is {:#x}, which exceeds uint16_t maximum (0xFFFF) for Wormhole.", args[0]));
        }
        arg0 = static_cast<uint16_t>(args[0]);
    }

    if (args.size() >= 2) {
        if (args[1] > 0xFFFF) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Argument 1 is {:#x}, which exceeds uint16_t maximum (0xFFFF) for Wormhole.", args[1]));
        }
        arg1 = static_cast<uint16_t>(args[1]);
    }

    // Serializes against other processes messaging any device's ARC; see the constructor for why the
    // lock is system-wide on Wormhole.
    auto lock = LockManager::acquire_mutex(MutexType::ARC_MSG);

    uint32_t fw_arg = arg0 | (arg1 << 16);
    write_to_arc_apb(&fw_arg, wormhole::ARC_RESET_SCRATCH_RES0_OFFSET, sizeof(uint32_t), noc_id);
    write_to_arc_apb(&msg_code, wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET, sizeof(uint32_t), noc_id);

    // On a remote device the writes above travel over ethernet; make sure they have landed before the
    // trigger bit is set. A no-op for a local device.
    if (remote_interface_ != nullptr) {
        remote_interface_->wait_for_non_mmio_flush();
    }

    uint32_t misc = 0;
    auto trigger_start = std::chrono::steady_clock::now();
    read_from_arc_apb(&misc, wormhole::ARC_RESET_ARC_MISC_CNTL_OFFSET, sizeof(uint32_t), noc_id);
    while (misc & (1 << 16)) {
        utils::check_timeout(
            trigger_start,
            timeout::ARC_TRIGGER_CLEAR_TIMEOUT,
            fmt::format(
                "Timed out after waiting {} ms for ARC to clear trigger bit on device {}",
                timeout::ARC_TRIGGER_CLEAR_TIMEOUT.count(),
                device_id_));
        read_from_arc_apb(&misc, wormhole::ARC_RESET_ARC_MISC_CNTL_OFFSET, sizeof(uint32_t), noc_id);
    }

    uint32_t val_wr = misc | (1 << 16);
    write_to_arc_apb(&val_wr, wormhole::ARC_RESET_ARC_MISC_CNTL_OFFSET, sizeof(uint32_t), noc_id);

    uint32_t status = 0xbadbad;
    uint32_t exit_code = 0;
    std::vector<uint32_t> return_values(2, 0);
    auto start = std::chrono::steady_clock::now();
    while (true) {
        read_from_arc_apb(&status, wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET, sizeof(uint32_t), noc_id);

        if ((status & 0xffff) == (msg_code & 0xff)) {
            // Both scratch result registers are read; the firmware leaves unused ones untouched, so a
            // caller that only cares about the first can ignore the second.
            read_from_arc_apb(return_values.data(), wormhole::ARC_RESET_SCRATCH_RES0_OFFSET, sizeof(uint32_t), noc_id);
            read_from_arc_apb(&return_values[1], wormhole::ARC_RESET_SCRATCH_RES1_OFFSET, sizeof(uint32_t), noc_id);

            exit_code = (status & 0xffff0000) >> 16;
            break;
        } else if (status == HANG_READ_VALUE) {
            log_warning(LogUMD, "On device {}, message code {:#x} not recognized by FW", device_id_, msg_code);
            exit_code = HANG_READ_VALUE;
            break;
        }

        utils::check_timeout(
            start,
            timeout,
            fmt::format(
                "Timed out after waiting {} ms for ARC to respond. Message code {:#x} with arguments {:#x} and {:#x}",
                timeout.count(),
                msg_code,
                arg0,
                arg1));
    }

    return DeviceCommandResult{exit_code, std::move(return_values)};
}

void WormholeDeviceFirmware::set_power_state(PowerState state, NocId noc_id) {
    // Power domains are only controllable over PCIe; JTAG and remote devices have no PcieInterface,
    // which matches what TTDevice::set_power_state did by returning early for them.
    if (pcie_interface_ == nullptr) {
        return;
    }
    pcie_interface_->set_power_state(state);
}

void WormholeDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {
    // The BUSY branch reads the info provider before send_device_command can run its own pre-init
    // check, so refuse here with the same error the deleted TTDevice path produced.
    if (firmware_info_provider_ == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, get_io_device_type(), device_id_, tt::ARCH::WORMHOLE_B0);
    }

    uint32_t msg_code = wormhole::ARC_MSG_COMMON_PREFIX;
    uint32_t target_aiclk = 0;
    switch (state) {
        case ClockState::BUSY:
            msg_code |= architecture_impl_->get_firmware_message_go_busy();
            target_aiclk = firmware_info_provider_->get_max_clock_freq().value_or(0);
            break;
        case ClockState::IDLE:
            msg_code |= architecture_impl_->get_firmware_message_go_idle();
            target_aiclk = wormhole::AICLK_IDLE_VAL;
            break;
        default:
            UMD_THROW(error::RuntimeError, "Unrecognized clock state.");
    }

    DeviceCommandResult result = send_device_command(msg_code, {0, 0}, timeout::ARC_MESSAGE_TIMEOUT, noc_id);
    UMD_ASSERT(
        result.exit_code == 0,
        error::RuntimeError,
        fmt::format("Failed to set clock state to {} with exit code: {}", (int)state, result.exit_code));

    wait_for_aiclk_value(target_aiclk, noc_id);
}

void WormholeDeviceFirmware::log_aiclk_timeout_warning(
    uint32_t target_aiclk, uint32_t observed_aiclk, std::chrono::milliseconds timeout_ms) {
    std::string arb_max_info;
    if (firmware_telemetry_reader_->is_entry_available(TelemetryTag::AICLK_ARB_MAX)) {
        const uint32_t arb_max = firmware_telemetry_reader_->read_entry(TelemetryTag::AICLK_ARB_MAX);
        arb_max_info = fmt::format(
            ", AICLK clamped by max-arbiter index {} at {} MHz", (arb_max >> 16) & 0xFFFF, arb_max & 0xFFFF);
    }

    log_warning(
        LogUMD,
        "AICLK failed to settle after {} ms. Expected {}, observed {}. ASIC temperature: {}{}",
        timeout_ms.count(),
        target_aiclk,
        observed_aiclk,
        firmware_info_provider_->get_asic_temperature().value_or(0.0),
        arb_max_info);

    if (firmware_telemetry_reader_->is_entry_available(TelemetryTag::UPDATE_TELEM_SPEED)) {
        const uint32_t update_telem_speed_ms = firmware_telemetry_reader_->read_entry(TelemetryTag::UPDATE_TELEM_SPEED);
        if (timeout_ms.count() <= update_telem_speed_ms) {
            log_warning(
                LogUMD,
                "AICLK timeout ({} ms) is not larger than the telemetry update interval ({} ms); the observed "
                "AICLK may be a stale telemetry value. Consider increasing AICLK_TIMEOUT.",
                timeout_ms.count(),
                update_telem_speed_ms);
        }
    }
}

void WormholeDeviceFirmware::wait_for_aiclk_value(
    uint32_t target_aiclk, NocId noc_id, std::chrono::milliseconds timeout_ms) {
    constexpr double AICLK_TOLERANCE_PERCENT = 5.0;

    uint32_t aiclk = 0;
    const bool settled = utils::poll_until(
        [&] {
            // Wormhole reads AICLK through an ARC message rather than telemetry. The 0xFFFF pair
            // packs to the firmware's no-argument sentinel, exactly as TTDevice::get_clock sends it;
            // a failed read throws rather than settling on garbage, as the deleted path did.
            DeviceCommandResult result = send_device_command(
                wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::GET_AICLK),
                {0xFFFF, 0xFFFF},
                timeout::ARC_MESSAGE_TIMEOUT,
                noc_id);
            if (result.exit_code != 0) {
                UMD_THROW(
                    error::RuntimeError, fmt::format("Failed to get AICLK value with exit code: {}", result.exit_code));
            }
            aiclk = result.return_values.at(0);
            return is_within_percentage(aiclk, target_aiclk, AICLK_TOLERANCE_PERCENT);
        },
        timeout_ms,
        std::chrono::microseconds(500),
        std::chrono::microseconds(100));

    if (!settled) {
        log_aiclk_timeout_warning(target_aiclk, aiclk, timeout_ms);
        return;
    }

    if (aiclk != target_aiclk) {
        log_warning(
            LogUMD,
            "AICLK settled at {} MHz, within {}% of the requested {} MHz but not an exact match. Proceeding.",
            aiclk,
            AICLK_TOLERANCE_PERCENT,
            target_aiclk);
    }
}

bool WormholeDeviceFirmware::get_noc_translation_enabled(NocId noc_id) {
    constexpr uint32_t ARC_APB_NIU_0_OFFSET = 0x50000;
    constexpr uint32_t NIU_CFG_0_OFFSET = 0x100;

    uint32_t niu_cfg = 0x0;
    read_from_arc_apb(&niu_cfg, ARC_APB_NIU_0_OFFSET + NIU_CFG_0_OFFSET, sizeof(niu_cfg), noc_id);
    return (niu_cfg & (1 << 14)) != 0;
}

ChipInfo WormholeDeviceFirmware::get_chip_info(NocId noc_id) {
    if (firmware_info_provider_ == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, get_io_device_type(), device_id_, tt::ARCH::WORMHOLE_B0);
    }
    ChipInfo chip_info;

    chip_info.noc_translation_enabled = get_noc_translation_enabled(noc_id);
    chip_info.board_id = firmware_info_provider_->get_board_id().value_or(0);
    chip_info.board_type = get_board_type_from_board_id(chip_info.board_id);
    chip_info.asic_location = firmware_info_provider_->get_asic_location().value_or(0);

    // Wormhole reports harvesting through an ARC message rather than telemetry, and only the tensix
    // mask is available.
    DeviceCommandResult result = send_device_command(
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::ARC_GET_HARVESTING),
        {0, 0},
        timeout::ARC_MESSAGE_TIMEOUT,
        noc_id);
    if (result.exit_code != 0) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Failed to get harvesting masks with exit code: {}", result.exit_code));
    }

    chip_info.harvesting_masks.tensix_harvesting_mask =
        CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, result.return_values.at(0));

    return chip_info;
}

tt_xy_pair WormholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1_ : arc_core_noc0_;
}

bool WormholeDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    auto start = std::chrono::steady_clock::now();
    while (get_eth_core_training_status(eth_core, noc_id) == EthTrainingStatus::IN_PROGRESS) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        if (duration > timeout_ms) {
            // UBB (6U) systems are known to leave links in training, so the timeout is only logged
            // there; every other board treats it as an error.
            if (firmware_info_provider_ != nullptr &&
                get_board_type_from_board_id(firmware_info_provider_->get_board_id().value_or(0)) == BoardType::UBB) {
                log_warning(
                    LogUMD,
                    "ETH training timed out after {} ms, on eth core {}, {}. Continuing for UBB board.",
                    timeout_ms.count(),
                    eth_core.x,
                    eth_core.y);
                return false;
            }
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "ETH training timed out after {} ms, on eth core {}, {}",
                    timeout_ms.count(),
                    eth_core.x,
                    eth_core.y));
        }
    }
    return true;
}

EthTrainingStatus WormholeDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    uint32_t retrain_status = 0;
    device_protocol_->read_ctrl(&retrain_status, eth_core, wormhole::ETH_RETRAIN_ADDR, sizeof(uint32_t), noc_id);
    // If the core is in retrain state the training status is not yet meaningful; the retrain state has
    // to clear first.
    if (retrain_status == wormhole::ETH_TRIGGER_RETRAIN_VAL) {
        log_trace(LogUMD, "Core {} is in retrain state, training is ongoing.", eth_core.str());
        return EthTrainingStatus::IN_PROGRESS;
    }

    uint32_t training_status = 0;
    device_protocol_->read_ctrl(&training_status, eth_core, wormhole::ETH_TRAIN_STATUS_ADDR, sizeof(uint32_t), noc_id);
    log_trace(LogUMD, "Training status for core {} is {}", eth_core.str(), training_status);

    if (training_status == static_cast<uint32_t>(EthTrainingStatus::FAIL)) {
        // Training can fail for many reasons; what matters here is telling an unconnected link apart
        // from a genuine failure on a connected one.
        uint32_t link_err_status = 0;
        device_protocol_->read_ctrl(
            &link_err_status, eth_core, wormhole::ETH_LINK_ERR_STATUS_ADDR, sizeof(uint32_t), noc_id);
        log_trace(LogUMD, "Link error status for core {} is {}", eth_core.str(), link_err_status);
        if (link_err_status >= wormhole::ETH_LINK_UNUSED_ERROR_CODE_RANGE_START) {
            return EthTrainingStatus::NOT_CONNECTED;
        }
    }
    return static_cast<EthTrainingStatus>(training_status);
}

bool WormholeDeviceFirmware::wait_dram_channel_training(
    uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    const uint32_t dram_banks_number = architecture_impl_->get_dram_banks_number();
    if (dram_channel >= dram_banks_number) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Invalid DRAM channel index {}, maximum index for given architecture is {}.",
                dram_channel,
                dram_banks_number - 1));
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status =
            firmware_info_provider_->get_dram_training_status(dram_banks_number);

        if (dram_training_status.empty()) {
            log_warning(LogUMD, "DRAM training status is not available, breaking the wait for DRAM training.");
            return false;
        }

        // Wormhole cannot retrain a channel, so a reported failure is terminal.
        if (dram_training_status.at(dram_channel) == DramTrainingStatus::FAIL) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format("DRAM training failed for channel {}; Wormhole cannot retrain it.", dram_channel));
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::SUCCESS) {
            return true;
        }

        utils::check_timeout(
            start,
            timeout_ms,
            fmt::format("DRAM training for channel {} timed out after {} ms", dram_channel, timeout_ms.count()));
    }
}

void WormholeDeviceFirmware::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.read(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

void WormholeDeviceFirmware::write_to_arc_apb(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.write(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

void WormholeDeviceFirmware::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_csm_.read(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

}  // namespace tt::umd
