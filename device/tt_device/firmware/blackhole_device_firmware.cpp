// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/firmware_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_info_provider_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "utils.hpp"

namespace tt::umd {

// How this class picks a route: a non-null JtagInterface means the device is reached over JTAG,
// otherwise it is reached over PCIe. Inferring the route from which optional interface is present
// is sound because a TTDevice is built for exactly one communication protocol - reaching the same
// chip over both PCIe and JTAG today requires two TTDevice objects, so the two interfaces are never
// both handed to the same firmware object. If UMD ever supports using both protocols against a
// single device, this has to become an explicit protocol selection rather than a null check.

BlackholeDeviceFirmware::BlackholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    architecture_impl_(architecture_impl),
    arc_apb_(device_protocol, pcie_interface, jtag_interface) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "BlackholeDeviceFirmware requires a DeviceProtocol.");
    UMD_ASSERT(
        architecture_impl_ != nullptr,
        error::RuntimeError,
        "BlackholeDeviceFirmware requires an ArchitectureImplementation.");
    // The exactly-one-transport invariant that get_io_device_type() and the ARC APB routing rely on
    // is enforced by arc_apb_, which is constructed from the same two interfaces above.

    // Read after the checks above, not in the member initialiser list: that runs first, so a null
    // protocol faulted there before the assert could report it.
    device_id_ = device_protocol_->get_mmio_id();

    // acquire_mutex() throws unless the mutex was initialized first, so claim it up front. For a
    // PCIe device this is the same key BlackholeArcMessenger uses (its device-number argument
    // defaults the type to PCIe), so this path and the messenger - still alive for tests - exclude
    // each other; the messenger never worked over JTAG, where this adds the missing key.
    LockManager::initialize_mutex(MutexType::ARC_MSG, device_id_, get_io_device_type());

    // Resolve both ARC coordinates once. The NOC translation state they depend on is fixed for the
    // device's lifetime and is read over BAR/JTAG, so this does not need the firmware to be up.
    const bool noc_translation_enabled = get_noc_translation_enabled();
    arc_core_noc0_ = blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/false);
    arc_core_noc1_ = blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/true);
}

BlackholeDeviceFirmware::~BlackholeDeviceFirmware() = default;

IODeviceType BlackholeDeviceFirmware::get_io_device_type() const {
    return jtag_interface_ != nullptr ? IODeviceType::JTAG : IODeviceType::PCIe;
}

void BlackholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // TODO: temporary. init_firmware() does two jobs - waiting for the firmware and building what
    // depends on it - so callers that only need the first (warm reset, for one) still run the
    // second, and a rebuild here would discard a live set and pay the telemetry table's readiness
    // wait again. The intended fix is to split the two into separate API calls, at which point
    // this guard goes away.
    //
    // Safe today only because every caller creates the TTDevice after a reset rather than reusing
    // one across it, so there is never pre-reset state to preserve.
    if (firmware_info_provider_ != nullptr) {
        return;
    }

    wait_firmware_ready(timeout_ms, noc_id);

    // The queue descriptor is published by the ARC firmware while it boots, so it can only be read
    // once the firmware is up.
    arc_msg_queue_ = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        device_protocol_,
        jtag_interface_,
        &arc_apb_,
        get_noc_translation_enabled(),
        BlackholeArcMessageQueueIndex::APPLICATION,
        noc_id);

    // The telemetry reader and info provider read state the firmware publishes, so this is the
    // earliest point they can exist.
    firmware_telemetry_reader_ = ArcTelemetryReader::create_arc_telemetry_reader(
        device_protocol_, tt::ARCH::BLACKHOLE, arc_core_noc0_, arc_core_noc1_);

    firmware_info_provider_ = FirmwareInfoProviderImplementation::create_firmware_info_provider(
        tt::ARCH::BLACKHOLE, device_protocol_, arc_core_noc0_, arc_core_noc1_, firmware_telemetry_reader_.get());
}

FirmwareTelemetryReader* BlackholeDeviceFirmware::get_firmware_telemetry_reader() const {
    return firmware_telemetry_reader_.get();
}

FirmwareInfoProvider* BlackholeDeviceFirmware::get_firmware_info_provider() const {
    return firmware_info_provider_.get();
}

DeviceCommandResult BlackholeDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout, NocId noc_id) {
    // No commands before the firmware is up: the queue descriptor it publishes during boot is what
    // the message queue is built from.
    if (arc_msg_queue_ == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, get_io_device_type(), device_id_, tt::ARCH::BLACKHOLE);
    }

    // Serializes against other processes messaging the same device's ARC.
    auto lock = LockManager::acquire_mutex(MutexType::ARC_MSG, device_id_, get_io_device_type());

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

void BlackholeDeviceFirmware::set_power_state(PowerState state, NocId noc_id) {
    // Power domains are only controllable over PCIe; JTAG and remote devices have no PcieInterface,
    // which matches what TTDevice::set_power_state did by returning early for them.
    if (pcie_interface_ == nullptr) {
        return;
    }
    pcie_interface_->set_power_state(state);
}

void BlackholeDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {
    // The BUSY branch reads the info provider before send_device_command can run its own pre-init
    // check, so refuse here with the same error the deleted TTDevice path produced.
    if (firmware_info_provider_ == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, get_io_device_type(), device_id_, tt::ARCH::BLACKHOLE);
    }

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

void BlackholeDeviceFirmware::log_aiclk_timeout_warning(
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

ChipInfo BlackholeDeviceFirmware::get_chip_info(NocId noc_id) {
    UMD_ASSERT(
        firmware_info_provider_ != nullptr && firmware_telemetry_reader_ != nullptr,
        error::UninitializedDeviceError,
        get_io_device_type(),
        device_id_,
        tt::ARCH::BLACKHOLE);
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

void BlackholeDeviceFirmware::wait_firmware_ready(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // Deliberate difference from the deleted TTDevice wait: its JTAG reads were pinned to the NOC0
    // ARC coordinate over the default NOC, and its non-AXI PCIe branch paired the selected NOC's
    // coordinate with the default NOC's routing. Every read here uses noc_id consistently,
    // coordinate and routing both; with the default NOC0 the two are bit-identical.
    // One throwaway read before the poll, so a dead ARC APB path faults on an access that is
    // clearly a probe rather than partway into the boot-status loop. This was
    // TTDevice::probe_arc(), moved here with its only caller.
    uint32_t dummy;
    read_from_arc_apb(&dummy, blackhole::ARC_RESET_SCRATCH_OFFSET, sizeof(dummy), noc_id);

    uint32_t arc_boot_status = 0;
    uint32_t arc_postcode = 0;
    uint32_t arc_error_status0 = 0;

    constexpr auto busy_poll_window = std::chrono::microseconds(1000);
    constexpr auto poll_interval = std::chrono::microseconds(10);
    const bool arc_core_started = utils::poll_until(
        [this, &arc_boot_status, &arc_postcode, &noc_id]() {
            read_from_arc_apb(&arc_boot_status, blackhole::SCRATCH_RAM_2, sizeof arc_boot_status, noc_id);
            read_from_arc_apb(&arc_postcode, blackhole::ARC_RESET_SCRATCH_OFFSET, sizeof arc_postcode, noc_id);
            return (arc_boot_status & 0x7) == 0x5;
        },
        timeout_ms,
        busy_poll_window,
        poll_interval);

    if (!arc_core_started) {
        read_from_arc_apb(&arc_error_status0, blackhole::SCRATCH_RAM_4, sizeof arc_error_status0, noc_id);
        UMD_THROW(
            error::FirmwareStartupError,
            get_io_device_type(),
            device_id_,
            tt::ARCH::BLACKHOLE,
            noc_id,
            get_firmware_noc_coord(noc_id),
            arc_boot_status,
            arc_postcode,
            timeout_ms,
            /*message_id=*/std::nullopt,
            arc_error_status0);
    }
}

bool BlackholeDeviceFirmware::get_noc_translation_enabled(NocId /*noc_id*/) {
    uint32_t niu_cfg;
    if (get_io_device_type() == IODeviceType::JTAG) {
        niu_cfg = jtag_interface_->mmio_read32(blackhole::NIU_CFG_NOC0_ARC_ADDR);
    } else {
        niu_cfg = pcie_interface_->bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + 0x100);
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
            // the timeout is only logged, matching the BlackholeTTDevice override this replaces.
            log_error(LogUMD, "ETH training timed out after {} ms", timeout_ms.count());
            return false;
        }
    }
    return true;
}

EthTrainingStatus BlackholeDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    uint32_t port_status_addr = blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, port_status);
    uint32_t port_status_val = 0;
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

uint64_t BlackholeDeviceFirmware::get_refclk_counter(NocId noc_id) {
    // Moved verbatim from TTDevice::get_refclk_counter, including its long-standing quirk: high2 is
    // never read back, so the wrap guard never fires. Kept as-is; fixing it is a behavior change.
    uint32_t high1_addr = 0;
    uint32_t high2_addr = 0;
    uint32_t low_addr = 0;
    read_from_arc_apb(&high1_addr, architecture_impl_->get_reset_unit_refclk_high_offset(), sizeof(high1_addr), noc_id);
    read_from_arc_apb(&low_addr, architecture_impl_->get_reset_unit_refclk_low_offset(), sizeof(low_addr), noc_id);
    read_from_arc_apb(&high1_addr, architecture_impl_->get_reset_unit_refclk_high_offset(), sizeof(high1_addr), noc_id);
    if (high2_addr > high1_addr) {
        read_from_arc_apb(&low_addr, architecture_impl_->get_reset_unit_refclk_low_offset(), sizeof(low_addr), noc_id);
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

void BlackholeDeviceFirmware::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.read(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

void BlackholeDeviceFirmware::write_to_arc_apb(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.write(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

}  // namespace tt::umd
