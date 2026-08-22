// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"

#include <fmt/ranges.h>

#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/arc/firmware_telemetry_reader.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/common.hpp"
#include "utils.hpp"

namespace tt::umd {

// How this class picks a protocol: a non-null JtagInterface means the device is reached over JTAG,
// otherwise it is reached over PCIe. Inferring the protocol from which optional interface is present
// is sound because a TTDevice is built for exactly one communication protocol - reaching the same
// chip over both PCIe and JTAG today requires two TTDevice objects, so the two interfaces are never
// both handed to the same firmware object. If UMD ever supports using both protocols against a
// single device, this has to become an explicit protocol selection rather than a null check.

BlackholeDeviceFirmware::BlackholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    architecture_implementation* architecture_impl,
    FirmwareInfoProvider* firmware_info_provider,
    FirmwareTelemetryReader* firmware_telemetry_reader) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    architecture_impl_(architecture_impl),
    firmware_info_provider_(firmware_info_provider),
    firmware_telemetry_reader_(firmware_telemetry_reader),
    device_id_(device_protocol->get_mmio_id()),
    arc_apb_(device_protocol, pcie_interface, jtag_interface, architecture_impl) {
    // add a throw here if architecture impl and device protocol are nullptr, pcie interface and jtag interface are
    // optional

    // acquire_mutex() throws unless the mutex was initialized first, so claim it up front the way
    // ArcMessenger's constructor does.
    lock_manager_.initialize_mutex(MutexType::ARC_MSG, device_id_, get_io_device_type());

    // Resolve both ARC coordinates once. The NOC translation state they depend on is fixed for the
    // device's lifetime and is read over BAR/JTAG, so this does not need the firmware to be up.
    const bool noc_translation_enabled = get_noc_translation_enabled();
    arc_core_noc0_ = blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/false);
    arc_core_noc1_ = blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/true);
}

IODeviceType BlackholeDeviceFirmware::get_io_device_type() const {
    return jtag_interface_ != nullptr ? IODeviceType::JTAG : IODeviceType::PCIe;
}

void BlackholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // Probe the ARC APB path before waiting on boot status, as TTDevice::init_tt_device does with
    // probe_arc() on the line right before wait_arc_core_start().
    uint32_t dummy;
    read_from_arc_apb(&dummy, architecture_impl_->get_arc_reset_scratch_offset(), sizeof(dummy), noc_id);

    uint32_t arc_boot_status = 0;
    uint32_t arc_postcode = 0;
    uint32_t arc_error_status0 = 0;

    constexpr auto busy_poll_window = std::chrono::microseconds(1000);
    constexpr auto poll_interval = std::chrono::microseconds(10);
    const bool arc_core_started = utils::poll_until(
        [this, &arc_boot_status, &arc_postcode, &noc_id]() {
            read_from_arc_apb(&arc_boot_status, blackhole::SCRATCH_RAM_2, sizeof arc_boot_status, noc_id);
            read_from_arc_apb(
                &arc_postcode, architecture_impl_->get_arc_reset_scratch_offset(), sizeof arc_postcode, noc_id);
            return (arc_boot_status & 0x7) == 0x5;
        },
        timeout_ms,
        busy_poll_window,
        poll_interval);

    if (!arc_core_started) {
        read_from_arc_apb(&arc_error_status0, blackhole::SCRATCH_RAM_4, sizeof arc_error_status0, noc_id);
        UMD_THROW(
            error::FirmwareStartupError,
            *this,
            noc_id,
            get_firmware_noc_coord(noc_id),
            arc_boot_status,
            arc_postcode,
            timeout_ms,
            /*message_id=*/std::nullopt,
            arc_error_status0);
    }

    // The queue descriptor is published by the ARC firmware while it boots, so it can only be read
    // once the firmware is up. This mirrors TTDevice::init_tt_device(), which builds the ARC
    // messenger on the line right after wait_arc_core_start().
    arc_msg_queue_ = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        device_protocol_,
        jtag_interface_,
        &arc_apb_,
        get_noc_translation_enabled(noc_id),
        BlackholeArcMessageQueueIndex::APPLICATION,
        noc_id);
}

DeviceCommandResult BlackholeDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout, NocId noc_id) {
    // Serializes against other processes messaging the same device's ARC.
    auto lock = lock_manager_.acquire_mutex(MutexType::ARC_MSG, device_id_, get_io_device_type());

    if (arc_msg_queue_ == nullptr) {
        UMD_THROW(error::RuntimeError, "ARC message queue is unavailable because init_firmware() has not been run.");
    }

    std::vector<uint32_t> return_values;
    uint32_t exit_code = arc_msg_queue_->send_message((ArcMessageType)msg_code, return_values, args, timeout, noc_id);

    log_debug(
        LogUMD,
        "ARC message 0x{:x} returned exit_code={} return_values=[{}]",
        msg_code,
        exit_code,
        fmt::join(return_values, ", "));

    return DeviceCommandResult{exit_code, std::move(return_values)};
}

ChipInfo BlackholeDeviceFirmware::get_chip_info(NocId noc_id) {
    if (firmware_info_provider_ == nullptr || firmware_telemetry_reader_ == nullptr) {
        UMD_THROW(
            error::RuntimeError,
            "Chip info is unavailable without a FirmwareInfoProvider and a FirmwareTelemetryReader.");
    }
    ChipInfo chip_info;

    chip_info.noc_translation_enabled = get_noc_translation_enabled(noc_id);
    chip_info.board_id = firmware_info_provider_->get_board_id().value_or(0);
    chip_info.board_type = get_board_type_from_board_id(chip_info.board_id);
    chip_info.asic_location = firmware_info_provider_->get_asic_location().value_or(0);

    chip_info.harvesting_masks.tensix_harvesting_mask = CoordinateManager::shuffle_tensix_harvesting_mask(
        tt::ARCH::BLACKHOLE,
        firmware_telemetry_reader_->is_entry_available(TelemetryTag::ENABLED_TENSIX_COL)
            ? (~firmware_telemetry_reader_->read_entry(TelemetryTag::ENABLED_TENSIX_COL) & 0x3FFF)
            : 0);
    chip_info.harvesting_masks.dram_harvesting_mask =
        firmware_telemetry_reader_->is_entry_available(TelemetryTag::ENABLED_GDDR)
            ? (~firmware_telemetry_reader_->read_entry(TelemetryTag::ENABLED_GDDR) & 0xFF)
            : 0;

    chip_info.harvesting_masks.eth_harvesting_mask =
        firmware_telemetry_reader_->is_entry_available(TelemetryTag::ENABLED_ETH)
            ? (~firmware_telemetry_reader_->read_entry(TelemetryTag::ENABLED_ETH) & 0x3FFF)
            : 0;

    chip_info.harvesting_masks.pcie_harvesting_mask = 0;
    if (firmware_telemetry_reader_->is_entry_available(TelemetryTag::PCIE_USAGE)) {
        uint32_t pcie_usage = firmware_telemetry_reader_->read_entry(TelemetryTag::PCIE_USAGE);

        uint32_t pcie0_usage = pcie_usage & 0x3;
        uint32_t pcie1_usage = (pcie_usage >> 2) & 0x3;

        const uint32_t pcie_usage_endpoint = 1;
        chip_info.harvesting_masks.pcie_harvesting_mask = 0;
        if (pcie0_usage != pcie_usage_endpoint) {
            chip_info.harvesting_masks.pcie_harvesting_mask |= 0x1;
        }

        if (pcie1_usage != pcie_usage_endpoint) {
            chip_info.harvesting_masks.pcie_harvesting_mask |= (1 << 1);
        }
    }

    chip_info.harvesting_masks.l2cpu_harvesting_mask = 0;
    if (firmware_telemetry_reader_->is_entry_available(TelemetryTag::ENABLED_L2CPU)) {
        chip_info.harvesting_masks.l2cpu_harvesting_mask = CoordinateManager::shuffle_l2cpu_harvesting_mask(
            tt::ARCH::BLACKHOLE, firmware_telemetry_reader_->read_entry(TelemetryTag::ENABLED_L2CPU));
    }

    return chip_info;
}

bool BlackholeDeviceFirmware::get_noc_translation_enabled(NocId /*noc_id*/) {
    uint32_t niu_cfg;
    const uint64_t addr = blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + 0x100;

    if (get_io_device_type() == IODeviceType::JTAG) {
        niu_cfg = jtag_interface_->mmio_read32(blackhole::NIU_CFG_NOC0_ARC_ADDR);
    } else {
        niu_cfg = pcie_interface_->bar_read32(addr);
    }
    return ((niu_cfg >> 14) & 0x1) != 0;
}

tt_xy_pair BlackholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1_ : arc_core_noc0_;
}

bool BlackholeDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // Port status is the last state to settle during the eth training sequence; IN_PROGRESS means
    // training has not finished yet.
    auto start = std::chrono::steady_clock::now();
    while (get_eth_core_training_status(eth_core, noc_id) == EthTrainingStatus::IN_PROGRESS) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        if (duration > timeout_ms) {
            // TODO: This should throw. ETH connections are very flaky on Blackhole right now, so
            // the timeout is only logged, matching BlackholeTTDevice::wait_eth_core_training.
            log_error(LogUMD, "ETH training timed out after {} ms", timeout_ms.count());
            return false;
        }
    }
    return true;
}

EthTrainingStatus BlackholeDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    uint32_t port_status_addr = blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, port_status);
    uint32_t port_status_val;
    device_protocol_->read_data(&port_status_val, eth_core, port_status_addr, sizeof(port_status_val), noc_id);
    return static_cast<EthTrainingStatus>(port_status_val);
}

bool BlackholeDeviceFirmware::wait_dram_channel_training(
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

    // Number of retrain attempts is chosen based on syseng team testing.
    constexpr uint32_t MAX_DRAM_RETRAIN_ATTEMPTS = 3;
    uint32_t num_retrain_dram_core = MAX_DRAM_RETRAIN_ATTEMPTS;
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status =
            firmware_info_provider_->get_dram_training_status(dram_banks_number);

        if (dram_training_status.empty()) {
            log_warning(LogUMD, "DRAM training status is not available, breaking the wait for DRAM training.");
            return false;
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::FAIL) {
            if (num_retrain_dram_core > 0) {
                log_warning(
                    LogUMD,
                    "DRAM training failed for channel {}, attempting retrain ({} attempts remaining).",
                    dram_channel,
                    num_retrain_dram_core - 1);
                retrain_dram_core(dram_channel, noc_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                num_retrain_dram_core--;
            } else {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format(
                        "DRAM training failed for channel {} after {} retrain attempts.",
                        dram_channel,
                        MAX_DRAM_RETRAIN_ATTEMPTS));
            }
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

DramTrainingStatus BlackholeDeviceFirmware::get_dram_channel_training_status(uint32_t dram_channel, NocId noc_id) {
    const uint32_t dram_banks_number = architecture_impl_->get_dram_banks_number();
    if (dram_channel >= dram_banks_number) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Invalid DRAM channel index {}, maximum index for given architecture is {}.",
                dram_channel,
                dram_banks_number - 1));
    }

    std::vector<DramTrainingStatus> dram_training_status =
        firmware_info_provider_->get_dram_training_status(dram_banks_number);
    if (dram_training_status.empty()) {
        UMD_THROW(error::RuntimeError, "DRAM training status is not available.");
    }
    return dram_training_status.at(dram_channel);
}

void BlackholeDeviceFirmware::retrain_dram_core(uint32_t dram_channel, NocId noc_id) {
    DeviceCommandResult result = send_device_command(
        static_cast<uint32_t>(blackhole::ArcMessageType::TOGGLE_GDDR_RESET),
        {dram_channel},
        timeout::ARC_MESSAGE_TIMEOUT,
        noc_id);
    if (result.exit_code != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to retrain DRAM core {} with exit code {}.", dram_channel, result.exit_code));
    }
}

void BlackholeDeviceFirmware::set_power_state(PowerState state, NocId noc_id) {
    // Power domains are only controllable over PCIe; JTAG and remote devices have no PcieInterface,
    // which matches TTDevice::set_power_state returning early for them.
    if (pcie_interface_ == nullptr) {
        return;
    }
    pcie_interface_->set_power_state(state);
}

void BlackholeDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {
    uint32_t msg_code = 0;
    uint32_t target_aiclk = 0;
    switch (state) {
        case ClockState::BUSY:
            msg_code = static_cast<uint32_t>(blackhole::ArcMessageType::AICLK_GO_BUSY);
            target_aiclk = firmware_info_provider_->get_max_clock_freq().value_or(0);
            break;
        case ClockState::IDLE:
            msg_code = static_cast<uint32_t>(blackhole::ArcMessageType::AICLK_GO_LONG_IDLE);
            target_aiclk = blackhole::AICLK_IDLE_VAL;
            break;
        default:
            UMD_THROW(error::RuntimeError, "Unrecognized clock state.");
    }

    DeviceCommandResult result = send_device_command(msg_code, {}, timeout::ARC_MESSAGE_TIMEOUT, noc_id);
    UMD_ASSERT(
        result.exit_code == 0,
        error::RuntimeError,
        fmt::format("Failed to set clock state to {} with exit code: {}", (int)state, result.exit_code));

    wait_for_aiclk_value(target_aiclk);
}

void BlackholeDeviceFirmware::wait_for_aiclk_value(uint32_t target_aiclk, std::chrono::milliseconds timeout_ms) {
    constexpr double AICLK_TOLERANCE_PERCENT = 5.0;

    uint32_t aiclk = 0;
    const bool settled = utils::poll_until(
        [&] {
            aiclk = firmware_telemetry_reader_->read_entry(TelemetryTag::AICLK);
            return is_within_percentage(aiclk, target_aiclk, AICLK_TOLERANCE_PERCENT);
        },
        timeout_ms,
        std::chrono::microseconds(500),
        std::chrono::microseconds(100));

    if (!settled) {
        log_warning(
            LogUMD, "AICLK did not reach {} MHz within {} ms. Proceeding anyway.", target_aiclk, timeout_ms.count());
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

void BlackholeDeviceFirmware::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.read(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

void BlackholeDeviceFirmware::write_to_arc_apb(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.write(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

}  // namespace tt::umd
