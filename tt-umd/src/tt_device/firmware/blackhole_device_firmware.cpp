// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/firmware/blackhole_device_firmware.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/arc/arc_telemetry_reader.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/coordinates/coordinate_manager.hpp"
#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/tt_device/tt_device_error.hpp"
#include "tt-umd/types/blackhole_arc.hpp"
#include "tt-umd/types/blackhole_eth.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/types/telemetry.hpp"

namespace tt::umd {

namespace {

// BOOT_STATUS (SCRATCH_RAM_2): bit 0 is set once the message queues are ready, bits 1-2 hold the hardware
// initialization state.
constexpr uint32_t BOOT_STATUS_MSG_QUEUE_READY = 1u << 0;
constexpr uint32_t BOOT_STATUS_HW_INIT_SHIFT = 1;
constexpr uint32_t BOOT_STATUS_HW_INIT_MASK = 0x3;
constexpr uint32_t HW_INIT_DONE = 2;
constexpr uint32_t HW_INIT_ERROR = 3;

uint32_t hw_init_state(uint32_t boot_status) {
    return (boot_status >> BOOT_STATUS_HW_INIT_SHIFT) & BOOT_STATUS_HW_INIT_MASK;
}

// NIU_CFG_0 sits at this offset in each NIU's register block; its bit 14 is set while the NIU translates
// coordinates.
constexpr uint32_t NIU_CFG_0_OFFSET = 0x100;
constexpr uint32_t NIU_CFG_NOC_ID_TRANSLATE_EN = 1u << 14;

// Each PCIe instance reports its use in two bits of PCIE_USAGE; 0 means the instance is not in use.
constexpr uint32_t PCIE_USAGE_BITS = 2;
constexpr uint32_t PCIE_USAGE_MASK = 0x3;

// Mask with the low num_units bits set.
uint32_t unit_mask(size_t num_units) { return static_cast<uint32_t>((1ull << num_units) - 1); }

}  // namespace

BlackholeDeviceFirmware::BlackholeDeviceFirmware(
    DeviceProtocol* device_protocol, PcieInterface* pcie_interface, ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    architecture_impl_(architecture_impl),
    arc_apb_(device_protocol, pcie_interface, nullptr) {
    // Translation is configured chip-wide, so the PCIe tile's own NIU tells the same as the ARC's. It answers over
    // BAR0 without NOC traffic, so creating the device neither stalls on nor misreads a hung NOC.
    const uint32_t niu_cfg = pcie_interface_->bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + NIU_CFG_0_OFFSET);
    noc_translation_enabled_ = (niu_cfg & NIU_CFG_NOC_ID_TRANSLATE_EN) != 0;
}

void BlackholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    uint32_t boot_status = 0;
    const bool boot_finished = utils::poll_until(
        [&]() {
            boot_status = read_arc_register(blackhole::SCRATCH_RAM_2, noc_id);
            const uint32_t hw_init = hw_init_state(boot_status);
            return hw_init == HW_INIT_ERROR ||
                   (hw_init == HW_INIT_DONE && (boot_status & BOOT_STATUS_MSG_QUEUE_READY) != 0);
        },
        timeout_ms,
        timeout::FIRMWARE_BUSY_POLL_WINDOW,
        timeout::FIRMWARE_POLL_INTERVAL,
        fmt::format("ARC firmware on device {} to boot", device_protocol_->get_mmio_id()));
    if (!boot_finished || hw_init_state(boot_status) == HW_INIT_ERROR) {
        const uint32_t postcode = read_arc_register(blackhole::ARC_RESET_SCRATCH_OFFSET, noc_id);
        const IODeviceType device_type = IODeviceType::PCIe;
        const int device_id = device_protocol_->get_mmio_id();
        const tt_xy_pair arc_core = get_firmware_noc_coord(noc_id);
        if (!boot_finished) {
            UMD_THROW(
                error::FirmwareStartupError,
                device_type,
                device_id,
                tt::ARCH::BLACKHOLE,
                noc_id,
                arc_core,
                boot_status,
                postcode,
                timeout_ms);
        }
        UMD_THROW(
            error::FirmwareStartupError,
            device_type,
            device_id,
            tt::ARCH::BLACKHOLE,
            noc_id,
            arc_core,
            boot_status,
            postcode);
    }

    const tt_xy_pair arc_core_noc0 = get_firmware_noc_coord(NocId::NOC0);
    const tt_xy_pair arc_core_noc1 = get_firmware_noc_coord(NocId::NOC1);
    auto message_queue = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        device_protocol_,
        nullptr,
        &arc_apb_,
        noc_translation_enabled_,
        BlackholeArcMessageQueueIndex::APPLICATION,
        noc_id);
    std::unique_ptr<ArcTelemetryReader> telemetry_reader;
    std::unique_ptr<FirmwareInfoProvider> firmware_info_provider;
    {
        // The reader locates its table over the thread's NOC, which the caller's choice has to override.
        NocIdSwitcher noc_switcher(noc_id);
        telemetry_reader = ArcTelemetryReader::create_arc_telemetry_reader(
            device_protocol_, tt::ARCH::BLACKHOLE, arc_core_noc0, arc_core_noc1);
        firmware_info_provider = FirmwareInfoProviderImplementation::create_firmware_info_provider(
            tt::ARCH::BLACKHOLE, device_protocol_, arc_core_noc0, arc_core_noc1, telemetry_reader.get());
    }

    const FirmwareBundleVersion firmware_version = firmware_info_provider->get_firmware_version(noc_id);
    const FirmwareBundleVersion minimum_version = get_minimum_compatible_firmware_version(tt::ARCH::BLACKHOLE);
    if (firmware_version < minimum_version) {
        log_warning(
            LogUMD,
            "Device {} runs firmware {}, older than the oldest supported {}; update the firmware.",
            device_protocol_->get_mmio_id(),
            firmware_version.to_string(),
            minimum_version.to_string());
    }
    log_debug(
        LogUMD, "ARC firmware {} on device {} is up.", firmware_version.to_string(), device_protocol_->get_mmio_id());

    message_queue_ = std::move(message_queue);
    telemetry_reader_ = std::move(telemetry_reader);
    firmware_info_provider_ = std::move(firmware_info_provider);
}

DeviceCommandResult BlackholeDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout, NocId noc_id) {
    require_initialized();
    if (msg_code > UINT8_MAX) {
        UMD_THROW(error::RuntimeError, fmt::format("{:#x} is not a Blackhole ARC message code.", msg_code));
    }
    DeviceCommandResult result;
    result.exit_code = message_queue_->send_message(
        static_cast<blackhole::ArcMessageType>(msg_code), result.return_values, args, timeout, noc_id);
    return result;
}

void BlackholeDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {
    const bool busy = state == ClockState::BUSY;
    send_device_command(
        busy ? architecture_impl_->get_firmware_message_go_busy() : architecture_impl_->get_firmware_message_go_idle(),
        {},
        timeout::ARC_MESSAGE_TIMEOUT,
        noc_id);

    const uint32_t idle_aiclk = architecture_impl_->get_min_clock_freq();
    uint32_t aiclk = 0;
    const bool settled = utils::poll_until(
        [&]() {
            aiclk = firmware_info_provider_->get_aiclk(noc_id).value_or(0);
            return busy ? aiclk > idle_aiclk : aiclk == idle_aiclk;
        },
        timeout::AICLK_TIMEOUT,
        timeout::FIRMWARE_BUSY_POLL_WINDOW,
        timeout::FIRMWARE_POLL_INTERVAL);
    if (!settled) {
        log_warning(
            LogUMD,
            "AICLK of device {} is still {} MHz {} ms after asking for the {} clock.",
            device_protocol_->get_mmio_id(),
            aiclk,
            timeout::AICLK_TIMEOUT.count(),
            busy ? "busy" : "idle");
    }
}

void BlackholeDeviceFirmware::set_power_state(PowerState state, NocId noc_id) {
    pcie_interface_->set_power_state(state);
}

bool BlackholeDeviceFirmware::get_noc_translation_enabled(NocId noc_id) {
    const uint64_t niu_cfg_addr =
        noc_id == NocId::NOC1 ? blackhole::NIU_CFG_NOC1_ARC_ADDR : blackhole::NIU_CFG_NOC0_ARC_ADDR;
    const uint32_t niu_cfg = read_arc_register(niu_cfg_addr - blackhole::ARC_NOC_XBAR_ADDRESS_START, noc_id);
    return (niu_cfg & NIU_CFG_NOC_ID_TRANSLATE_EN) != 0;
}

ChipInfo BlackholeDeviceFirmware::get_chip_info(NocId noc_id) {
    require_initialized();
    ChipInfo chip_info;
    chip_info.noc_translation_enabled = get_noc_translation_enabled(noc_id);
    chip_info.board_id = firmware_info_provider_->get_board_id(noc_id).value();
    chip_info.board_type = get_board_type_from_board_id(chip_info.board_id);
    chip_info.asic_location = firmware_info_provider_->get_asic_location(noc_id).value();

    // The firmware reports enabled units in its own numbering; the SoC model takes harvested units in NOC0 order.
    HarvestingMasks& masks = chip_info.harvesting_masks;
    const uint32_t tensix_harvested_physical =
        ~telemetry_reader_->read_entry(TelemetryTag::ENABLED_TENSIX_COL, noc_id) &
        unit_mask(blackhole::T6_X_LOCATIONS.size());
    masks.tensix_harvesting_mask =
        CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::BLACKHOLE, tensix_harvested_physical);
    masks.dram_harvesting_mask =
        ~telemetry_reader_->read_entry(TelemetryTag::ENABLED_GDDR, noc_id) & unit_mask(blackhole::NUM_DRAM_BANKS);
    masks.eth_harvesting_mask =
        ~telemetry_reader_->read_entry(TelemetryTag::ENABLED_ETH, noc_id) & unit_mask(blackhole::NUM_ETH_CHANNELS);
    masks.l2cpu_harvesting_mask = CoordinateManager::shuffle_l2cpu_harvesting_mask(
        tt::ARCH::BLACKHOLE, telemetry_reader_->read_entry(TelemetryTag::ENABLED_L2CPU, noc_id));

    const uint32_t pcie_usage = telemetry_reader_->read_entry(TelemetryTag::PCIE_USAGE, noc_id);
    for (size_t pcie_index = 0; pcie_index < blackhole::PCIE_CORES_NOC0.size(); pcie_index++) {
        if (((pcie_usage >> (pcie_index * PCIE_USAGE_BITS)) & PCIE_USAGE_MASK) == 0) {
            masks.pcie_harvesting_mask |= 1u << pcie_index;
        }
    }
    return chip_info;
}

tt_xy_pair BlackholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const {
    return blackhole::get_arc_core(noc_translation_enabled_, noc_id == NocId::NOC1);
}

bool BlackholeDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return utils::poll_until(
        [&]() { return get_eth_core_training_status(eth_core, noc_id) != EthTrainingStatus::IN_PROGRESS; },
        timeout_ms,
        timeout::FIRMWARE_BUSY_POLL_WINDOW,
        timeout::FIRMWARE_POLL_INTERVAL,
        fmt::format("ETH core {} on device {} to train", eth_core.str(), device_protocol_->get_mmio_id()));
}

EthTrainingStatus BlackholeDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    uint32_t port_status = 0;
    device_protocol_->read_ctrl(
        &port_status,
        eth_core,
        blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, port_status),
        sizeof(port_status),
        noc_id,
        RegisterAccessWidth::BITS32);
    switch (port_status) {
        case blackhole::PORT_UP:
            return EthTrainingStatus::SUCCESS;
        case blackhole::PORT_DOWN:
            return EthTrainingStatus::FAIL;
        case blackhole::PORT_UNUSED:
            return EthTrainingStatus::NOT_CONNECTED;
        default:
            return EthTrainingStatus::IN_PROGRESS;
    }
}

bool BlackholeDeviceFirmware::wait_dram_channel_training(
    uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    require_initialized();
    DramTrainingStatus channel_status = DramTrainingStatus::IN_PROGRESS;
    const bool finished = utils::poll_until(
        [&]() {
            const std::vector<DramTrainingStatus> statuses =
                firmware_info_provider_->get_dram_training_status(dram_channel + 1, noc_id);
            channel_status = statuses.empty() ? DramTrainingStatus::IN_PROGRESS : statuses.back();
            return channel_status != DramTrainingStatus::IN_PROGRESS;
        },
        timeout_ms,
        timeout::FIRMWARE_BUSY_POLL_WINDOW,
        timeout::FIRMWARE_POLL_INTERVAL,
        fmt::format("DRAM channel {} on device {} to train", dram_channel, device_protocol_->get_mmio_id()));
    if (channel_status == DramTrainingStatus::FAIL) {
        log_warning(
            LogUMD, "DRAM channel {} on device {} failed training.", dram_channel, device_protocol_->get_mmio_id());
    }
    return finished;
}

std::optional<uint64_t> BlackholeDeviceFirmware::get_runtime_telemetry_buffer_address(NocId noc_id) {
    require_initialized();
    // The firmware publishes an ARC CSM address, which the ARC tile serves at the same NOC address.
    const uint32_t address = read_arc_register(blackhole::SCRATCH_RAM_22, noc_id);
    if (address == 0) {
        return std::nullopt;
    }
    return address;
}

std::optional<uint32_t> BlackholeDeviceFirmware::get_runtime_telemetry_buffer_size(NocId noc_id) {
    // Published together with the address, so a buffer without an address has no size either.
    if (!get_runtime_telemetry_buffer_address(noc_id).has_value()) {
        return std::nullopt;
    }
    return read_arc_register(blackhole::SCRATCH_RAM_23, noc_id);
}

uint64_t BlackholeDeviceFirmware::get_refclk_counter(NocId noc_id) {
    const uint32_t low_offset = architecture_impl_->get_reset_unit_refclk_low_offset();
    const uint32_t high_offset = architecture_impl_->get_reset_unit_refclk_high_offset();
    // Read the high word around the low one, so a carry between the two reads cannot tear the value.
    uint32_t high = read_arc_register(high_offset, noc_id);
    uint32_t low = read_arc_register(low_offset, noc_id);
    const uint32_t high_after = read_arc_register(high_offset, noc_id);
    if (high_after != high) {
        high = high_after;
        low = read_arc_register(low_offset, noc_id);
    }
    return (static_cast<uint64_t>(high) << 32) | low;
}

FirmwareTelemetryReader* BlackholeDeviceFirmware::get_firmware_telemetry_reader() const {
    return telemetry_reader_.get();
}

FirmwareInfoProvider* BlackholeDeviceFirmware::get_firmware_info_provider() const {
    return firmware_info_provider_.get();
}

void BlackholeDeviceFirmware::require_initialized() const {
    if (firmware_info_provider_ == nullptr) {
        UMD_THROW(
            error::UninitializedDeviceError, IODeviceType::PCIe, device_protocol_->get_mmio_id(), tt::ARCH::BLACKHOLE);
    }
}

uint32_t BlackholeDeviceFirmware::read_arc_register(uint32_t arc_addr_offset, NocId noc_id) {
    uint32_t value = 0;
    arc_apb_.read(&value, arc_addr_offset, sizeof(value), get_firmware_noc_coord(noc_id), noc_id);
    return value;
}

}  // namespace tt::umd
