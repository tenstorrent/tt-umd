// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/firmware/blackhole_device_firmware.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tt-umd/arc/arc_telemetry_reader.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/architecture_registers.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/coordinates/coordinate_manager.hpp"
#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/tt_device/tt_device_error.hpp"
#include "tt-umd/types/blackhole_arc.hpp"
#include "tt-umd/types/blackhole_eth.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/timeouts.hpp"

namespace tt::umd {

namespace {

// BOOT_STATUS0 (SCRATCH_RAM_2): bit 0 says the message queues accept messages, bits [2:1] how hardware init went.
constexpr uint32_t BOOT_STATUS_MSG_QUEUE_READY = 1u << 0;
constexpr uint32_t BOOT_STATUS_HW_INIT_SHIFT = 1;
constexpr uint32_t BOOT_STATUS_HW_INIT_MASK = 0x3;
constexpr uint32_t HW_INIT_DONE = 2;
constexpr uint32_t HW_INIT_ERROR = 3;
constexpr uint32_t BOOT_STATUS_FW_ID_SHIFT = 3;
constexpr uint32_t BOOT_STATUS_FW_ID_MASK = 0xF;
constexpr uint32_t FW_ID_RECOVERY = 1;

// SCRATCH_0 holds the firmware's post code, which locates where a boot got stuck.
constexpr uint32_t ARC_POSTCODE_OFFSET = blackhole::ARC_RESET_SCRATCH_OFFSET;

// NIU_CFG_0 of the host-facing PCIe tile, and the bit that turns coordinate translation on for that NOC.
constexpr uint32_t NIU_CFG_0_OFFSET = 0x100;
constexpr uint32_t NIU_CFG_0_NOC_ID_TRANSLATE_EN = 1u << 14;

// The firmware publishes the runtime telemetry buffer in SCRATCH_RAM_22/23 since this bundle.
const FirmwareBundleVersion RUNTIME_TELEMETRY_BUFFER_FIRMWARE_VERSION(19, 12, 0);

// PCIE_USAGE packs a 2-bit mode per PCIe instance; only an endpoint instance is reachable from the host.
constexpr uint32_t PCIE_USAGE_BITS_PER_INSTANCE = 2;
constexpr uint32_t PCIE_USAGE_MODE_MASK = 0x3;
constexpr uint32_t PCIE_MODE_ENDPOINT = 1;
constexpr size_t PCIE_INSTANCE_COUNT = 2;

constexpr uint32_t TENSIX_COLUMN_MASK = 0x3FFF;
constexpr uint32_t ETH_INSTANCE_MASK = 0x3FFF;
constexpr uint32_t GDDR_INSTANCE_MASK = 0xFF;

// AICLK telemetry refreshes once per telemetry interval, so the settled clock is accepted within this margin.
constexpr uint32_t AICLK_SETTLED_TOLERANCE_MHZ = 10;

// Polls ready() until it holds or timeout passes: busy for the first moments, since firmware state often is already
// there, then at the firmware poll interval. Returns whether ready() held.
template <typename Predicate>
bool poll_firmware(Predicate ready, std::chrono::milliseconds timeout) {
    const auto start = std::chrono::steady_clock::now();
    while (!ready()) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > timeout) {
            return false;
        }
        if (elapsed > timeout::FIRMWARE_BUSY_POLL_WINDOW) {
            std::this_thread::sleep_for(timeout::FIRMWARE_POLL_INTERVAL);
        }
    }
    return true;
}

EthTrainingStatus to_eth_training_status(uint32_t port_status) {
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

size_t pcie_harvesting_mask_from_usage(uint32_t pcie_usage) {
    size_t mask = 0;
    for (size_t instance = 0; instance < PCIE_INSTANCE_COUNT; instance++) {
        const uint32_t mode = (pcie_usage >> (instance * PCIE_USAGE_BITS_PER_INSTANCE)) & PCIE_USAGE_MODE_MASK;
        if (mode != PCIE_MODE_ENDPOINT) {
            mask |= 1u << instance;
        }
    }
    return mask;
}

}  // namespace

BlackholeDeviceFirmware::BlackholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    architecture_impl_(architecture_impl),
    arc_apb_(device_protocol, pcie_interface, jtag_interface) {}

BlackholeDeviceFirmware::~BlackholeDeviceFirmware() = default;

void BlackholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // The message queues are usable once bit 0 is up, but harvesting and other static telemetry are final only when
    // hardware init has finished, with or without errors.
    uint32_t boot_status = 0;
    const bool booted = poll_firmware(
        [&]() {
            boot_status = read_arc_register(blackhole::SCRATCH_RAM_2, noc_id);
            const uint32_t hw_init = (boot_status >> BOOT_STATUS_HW_INIT_SHIFT) & BOOT_STATUS_HW_INIT_MASK;
            return boot_status != HANG_READ_VALUE && (boot_status & BOOT_STATUS_MSG_QUEUE_READY) != 0 &&
                   (hw_init == HW_INIT_DONE || hw_init == HW_INIT_ERROR);
        },
        timeout_ms);
    if (!booted) {
        UMD_THROW(
            error::FirmwareStartupError,
            IODeviceType::PCIe,
            device_protocol_->get_mmio_id(),
            tt::ARCH::BLACKHOLE,
            noc_id,
            get_firmware_noc_coord(noc_id),
            boot_status,
            read_arc_register(ARC_POSTCODE_OFFSET, noc_id),
            timeout_ms,
            std::nullopt,
            read_arc_register(blackhole::SCRATCH_RAM_4, noc_id));
    }

    if (((boot_status >> BOOT_STATUS_HW_INIT_SHIFT) & BOOT_STATUS_HW_INIT_MASK) == HW_INIT_ERROR) {
        log_warning(
            LogUMD,
            "Blackhole firmware on device {} reported a hardware init error: {}.",
            device_protocol_->get_mmio_id(),
            blackhole::interpret_smc_init_status(read_arc_register(blackhole::SCRATCH_RAM_4, noc_id)));
    }
    if (((boot_status >> BOOT_STATUS_FW_ID_SHIFT) & BOOT_STATUS_FW_ID_MASK) == FW_ID_RECOVERY) {
        log_warning(
            LogUMD,
            "Device {} runs recovery firmware, which publishes no telemetry; flash a mission firmware bundle.",
            device_protocol_->get_mmio_id());
    }

    const bool translation_enabled = is_noc_translation_enabled(noc_id);
    arc_message_queue_ = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        device_protocol_, nullptr, &arc_apb_, translation_enabled, BlackholeArcMessageQueueIndex::APPLICATION, noc_id);

    const tt_xy_pair arc_core_noc0 = blackhole::get_arc_core(translation_enabled, false);
    const tt_xy_pair arc_core_noc1 = blackhole::get_arc_core(translation_enabled, true);
    // Both read the telemetry table while they are built, which goes over the thread's NOC.
    NocIdSwitcher noc_switcher(noc_id);
    telemetry_reader_ = ArcTelemetryReader::create_arc_telemetry_reader(
        device_protocol_, tt::ARCH::BLACKHOLE, arc_core_noc0, arc_core_noc1);
    firmware_info_provider_ = FirmwareInfoProviderImplementation::create_firmware_info_provider(
        tt::ARCH::BLACKHOLE, device_protocol_, arc_core_noc0, arc_core_noc1, telemetry_reader_.get());

    log_debug(
        LogUMD,
        "Device {} runs Blackhole firmware bundle {}.",
        device_protocol_->get_mmio_id(),
        firmware_info_provider_->get_firmware_version(noc_id).to_string());
}

DeviceCommandResult BlackholeDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout, NocId noc_id) {
    throw_if_uninitialized();
    // The queue's request header carries only the message ID; IDs whose header needs extra fields are not supported.
    UMD_ASSERT(
        msg_code <= 0xFF,
        error::RuntimeError,
        fmt::format("Blackhole firmware command {:#x} does not fit the 8-bit message ID.", msg_code));

    DeviceCommandResult result;
    result.exit_code = arc_message_queue_->send_message(
        static_cast<blackhole::ArcMessageType>(msg_code), result.return_values, args, timeout, noc_id);
    return result;
}

void BlackholeDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {
    throw_if_uninitialized();
    const blackhole::ArcMessageType message = state == ClockState::BUSY ? blackhole::ArcMessageType::AICLK_GO_BUSY
                                                                        : blackhole::ArcMessageType::AICLK_GO_LONG_IDLE;
    const DeviceCommandResult result =
        send_device_command(static_cast<uint32_t>(message), {}, timeout::ARC_MESSAGE_TIMEOUT, noc_id);
    UMD_ASSERT(
        result.exit_code == 0,
        error::RuntimeError,
        fmt::format("Firmware refused the AICLK request with status {:#x}.", result.exit_code));

    const uint32_t target_mhz =
        state == ClockState::BUSY
            ? firmware_info_provider_->get_max_clock_freq(noc_id).value_or(blackhole::AICLK_BUSY_VAL)
            : architecture_impl_->get_min_clock_freq();
    wait_for_aiclk(target_mhz, noc_id);
}

void BlackholeDeviceFirmware::set_power_state(PowerState state, NocId /*noc_id*/) {
    // The KMD combines the power votes of every process holding the device and sends them to the firmware as one
    // request, overriding any request sent past it, so power goes through the KMD vote.
    UMD_ASSERT(
        pcie_interface_ != nullptr,
        error::RuntimeError,
        "Blackhole power states can only be requested for a device reached over PCIe.");
    pcie_interface_->set_power_state(state);
}

bool BlackholeDeviceFirmware::get_noc_translation_enabled(NocId noc_id) { return is_noc_translation_enabled(noc_id); }

ChipInfo BlackholeDeviceFirmware::get_chip_info(NocId noc_id) {
    throw_if_uninitialized();
    FirmwareTelemetryReader& telemetry = *telemetry_reader_;
    const auto read_tag = [&](uint8_t tag) { return telemetry.read_entry(tag, noc_id); };
    const auto read_tag_or_zero = [&](uint8_t tag) {
        return telemetry.is_entry_available(tag, noc_id) ? telemetry.read_entry(tag, noc_id) : 0u;
    };

    ChipInfo chip_info;
    chip_info.noc_translation_enabled = is_noc_translation_enabled(noc_id);

    // The firmware publishes enable bitmaps by its own instance numbering; Tensix and L2CPU masks are reordered to
    // NOC0 order, while ETH and GDDR masks are used in instance order as they are.
    const uint32_t tensix_enabled = read_tag(TelemetryTag::ENABLED_TENSIX_COL);
    chip_info.harvesting_masks.tensix_harvesting_mask =
        CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::BLACKHOLE, ~tensix_enabled & TENSIX_COLUMN_MASK);
    chip_info.harvesting_masks.eth_harvesting_mask = ~read_tag(TelemetryTag::ENABLED_ETH) & ETH_INSTANCE_MASK;
    chip_info.harvesting_masks.dram_harvesting_mask = ~read_tag(TelemetryTag::ENABLED_GDDR) & GDDR_INSTANCE_MASK;
    chip_info.harvesting_masks.l2cpu_harvesting_mask =
        CoordinateManager::shuffle_l2cpu_harvesting_mask(tt::ARCH::BLACKHOLE, read_tag(TelemetryTag::ENABLED_L2CPU));
    if (telemetry.is_entry_available(TelemetryTag::PCIE_USAGE, noc_id)) {
        chip_info.harvesting_masks.pcie_harvesting_mask =
            pcie_harvesting_mask_from_usage(telemetry.read_entry(TelemetryTag::PCIE_USAGE, noc_id));
    }

    chip_info.board_id =
        (static_cast<uint64_t>(read_tag(TelemetryTag::BOARD_ID_HIGH)) << 32) | read_tag(TelemetryTag::BOARD_ID_LOW);
    chip_info.board_type = get_board_type_from_board_id(chip_info.board_id);
    chip_info.asic_location = static_cast<uint8_t>(read_tag_or_zero(TelemetryTag::ASIC_LOCATION));
    return chip_info;
}

tt_xy_pair BlackholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const {
    return blackhole::get_arc_core(is_noc_translation_enabled(noc_id), noc_id == NocId::NOC1);
}

bool BlackholeDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return poll_firmware(
        [&]() { return get_eth_core_training_status(eth_core, noc_id) != EthTrainingStatus::IN_PROGRESS; }, timeout_ms);
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
    return to_eth_training_status(port_status);
}

bool BlackholeDeviceFirmware::wait_dram_channel_training(
    uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    throw_if_uninitialized();
    DramTrainingStatus status = DramTrainingStatus::IN_PROGRESS;
    poll_firmware(
        [&]() {
            const std::vector<DramTrainingStatus> statuses =
                firmware_info_provider_->get_dram_training_status(dram_channel + 1, noc_id);
            // Firmware that publishes no training status leaves nothing to wait for.
            status = statuses.size() > dram_channel ? statuses[dram_channel] : DramTrainingStatus::SUCCESS;
            return status != DramTrainingStatus::IN_PROGRESS;
        },
        timeout_ms);
    return status == DramTrainingStatus::SUCCESS;
}

std::optional<uint64_t> BlackholeDeviceFirmware::get_runtime_telemetry_buffer_address(NocId noc_id) {
    const auto buffer = read_runtime_telemetry_buffer(noc_id);
    return buffer ? std::optional<uint64_t>(buffer->first) : std::nullopt;
}

std::optional<uint32_t> BlackholeDeviceFirmware::get_runtime_telemetry_buffer_size(NocId noc_id) {
    const auto buffer = read_runtime_telemetry_buffer(noc_id);
    return buffer ? std::optional<uint32_t>(buffer->second) : std::nullopt;
}

uint64_t BlackholeDeviceFirmware::get_refclk_counter(NocId noc_id) {
    // The two halves are separate reads, so re-read when the high half rolled over in between.
    uint32_t high = read_arc_register(blackhole::ARC_RESET_REFCLK_HIGH_OFFSET, noc_id);
    uint32_t low = 0;
    while (true) {
        low = read_arc_register(blackhole::ARC_RESET_REFCLK_LOW_OFFSET, noc_id);
        const uint32_t high_again = read_arc_register(blackhole::ARC_RESET_REFCLK_HIGH_OFFSET, noc_id);
        if (high_again == high) {
            break;
        }
        high = high_again;
    }
    return (static_cast<uint64_t>(high) << 32) | low;
}

FirmwareTelemetryReader* BlackholeDeviceFirmware::get_firmware_telemetry_reader() const {
    return telemetry_reader_.get();
}

FirmwareInfoProvider* BlackholeDeviceFirmware::get_firmware_info_provider() const {
    return firmware_info_provider_.get();
}

bool BlackholeDeviceFirmware::is_noc_translation_enabled(NocId noc_id) const {
    UMD_ASSERT(
        pcie_interface_ != nullptr,
        error::RuntimeError,
        "Blackhole NOC translation state can only be read from a device reached over PCIe.");
    // The firmware programs translation into every NIU of a NOC, so the PCIe tile's own NIU speaks for the chip and
    // its registers are in BAR0, reachable without knowing where ARC sits on the NOC.
    const uint32_t niu_base =
        noc_id == NocId::NOC1 ? blackhole::NIU_CFG_NOC1_BAR_PCIE_ADDR : blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR;
    return (pcie_interface_->bar_read32(niu_base + NIU_CFG_0_OFFSET) & NIU_CFG_0_NOC_ID_TRANSLATE_EN) != 0;
}

std::optional<std::pair<uint64_t, uint32_t>> BlackholeDeviceFirmware::read_runtime_telemetry_buffer(NocId noc_id) {
    throw_if_uninitialized();
    if (firmware_info_provider_->get_firmware_version(noc_id) < RUNTIME_TELEMETRY_BUFFER_FIRMWARE_VERSION) {
        return std::nullopt;
    }
    // The firmware publishes an ARC address, which is also the buffer's NOC address on the ARC tile.
    const uint32_t address = read_arc_register(blackhole::SCRATCH_RAM_22, noc_id);
    const uint32_t size = read_arc_register(blackhole::SCRATCH_RAM_23, noc_id);
    if (address == 0 || size == 0) {
        return std::nullopt;
    }
    return std::make_pair(static_cast<uint64_t>(address), size);
}

uint32_t BlackholeDeviceFirmware::read_arc_register(uint32_t arc_addr_offset, NocId noc_id) {
    uint32_t value = 0;
    arc_apb_.read(&value, arc_addr_offset, sizeof(value), get_firmware_noc_coord(noc_id), noc_id);
    return value;
}

void BlackholeDeviceFirmware::throw_if_uninitialized() const {
    if (!arc_message_queue_ || !firmware_info_provider_) {
        UMD_THROW(
            error::UninitializedDeviceError,
            pcie_interface_ != nullptr ? IODeviceType::PCIe : IODeviceType::JTAG,
            device_protocol_->get_mmio_id(),
            tt::ARCH::BLACKHOLE);
    }
}

void BlackholeDeviceFirmware::wait_for_aiclk(uint32_t target_mhz, NocId noc_id) {
    uint32_t aiclk = 0;
    const bool settled = poll_firmware(
        [&]() {
            aiclk = firmware_info_provider_->get_aiclk(noc_id).value_or(target_mhz);
            const uint32_t distance = aiclk > target_mhz ? aiclk - target_mhz : target_mhz - aiclk;
            return distance <= AICLK_SETTLED_TOLERANCE_MHZ;
        },
        timeout::AICLK_TIMEOUT);
    if (!settled) {
        // Throttling or another client keeping the chip busy can hold AICLK away from the target.
        log_warning(
            LogUMD,
            "AICLK on device {} is {} MHz, not the requested {} MHz, after {} ms.",
            device_protocol_->get_mmio_id(),
            aiclk,
            target_mhz,
            timeout::AICLK_TIMEOUT.count());
    }
}

}  // namespace tt::umd
