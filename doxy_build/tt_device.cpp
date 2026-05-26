// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device.hpp"

#include <stdexcept>

#include "tt_device_model.hpp"

namespace tt::umd {

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) {
    device_protocol_ = model_->create_device_protocol();
    device_firmware_ = model_->create_device_firmware();
    architecture_impl_ = model_->create_architecture_impl();

    hang_detector_ = model_->create_hang_detector();

    pcie_interface_ = dynamic_cast<PcieInterface *>(device_protocol_.get());
    dma_interface_ = dynamic_cast<DmaInterface *>(device_protocol_.get());
    io_window_factory_ = dynamic_cast<IoWindowFactory *>(device_protocol_.get());
    jtag_interface_ = dynamic_cast<JtagInterface *>(device_protocol_.get());
    remote_interface_ = dynamic_cast<RemoteInterface *>(device_protocol_.get());

    auto info = model_->create_device_info();
    arch_ = info->get_arch();
    device_type_ = info->get_device_type();
    device_id_ = info->get_device_id();
    is_remote_ = info->is_remote();
}

TTDevice::~TTDevice() = default;

void TTDevice::init_device(std::chrono::milliseconds timeout_ms) {
    device_firmware_->wait_firmware_startup(timeout_ms);

    firmware_telemetry_reader_ = model_->create_firmware_telemetry_reader();
    firmware_info_provider_ = model_->create_firmware_info_provider();

    soc_descriptor_.emplace(std::make_shared<SocArchDescriptor>(arch_), device_firmware_->get_chip_info());
}

tt_xy_pair TTDevice::translate(CoreCoord core) const {
    if (core.coord_system == tt::CoordSystem::LITERAL) {
        return tt_xy_pair(core.x, core.y);
    }
    return get_soc_descriptor().translate_chip_coord_to_translated(core);
}

// --- Data I/O ---

void TTDevice::read_data(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc) {
    device_protocol_->read_data(dst, translate(core), addr, size, noc);
}

void TTDevice::write_data(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc) {
    device_protocol_->write_data(src, translate(core), addr, size, noc);
}

void TTDevice::read_ctrl(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc) {
    device_protocol_->read_ctrl(dst, translate(core), addr, size, noc);
}

void TTDevice::write_ctrl(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc) {
    device_protocol_->write_ctrl(src, translate(core), addr, size, noc);
}

void TTDevice::write_to_core_range(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc) {
    device_protocol_->write_to_core_range(src, translate(core_start), translate(core_end), addr, size, noc);
}

void TTDevice::write_to_core_range(const void *src, size_t size, uint64_t addr, NocId noc) {}

// --- DMA ---

void TTDevice::dma_write_to_core_range(const void *src, uint64_t dst_addr, size_t size, CoreCoord core) {
    dma_interface_->dma_write(src, dst_addr, size, translate(core), NocId::DEFAULT_NOC);
}

void TTDevice::dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core) {
    dma_interface_->dma_read(dst, src_addr, size, translate(core), NocId::DEFAULT_NOC);
}

void TTDevice::dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end) {
    dma_interface_->dma_multicast_write(
        src, dst_addr, size, translate(core_start), translate(core_end), NocId::DEFAULT_NOC);
}

void TTDevice::dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core) {
    dma_interface_->dma_write_zero_copy(src_iova, dst_addr, size, translate(core), NocId::DEFAULT_NOC);
}

void TTDevice::dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core) {
    dma_interface_->dma_read_zero_copy(dst_iova, src_addr, size, translate(core), NocId::DEFAULT_NOC);
}

void TTDevice::dma_write_to_core_range_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end) {
    dma_interface_->dma_multicast_write_zero_copy(
        src_iova, dst_addr, size, translate(core_start), translate(core_end), NocId::DEFAULT_NOC);
}

// --- DeviceFirmware ---

void TTDevice::wait_firmware_startup(const std::chrono::milliseconds timeout_ms) {
    device_firmware_->wait_firmware_startup(timeout_ms);
}

std::chrono::milliseconds TTDevice::wait_eth_core_training(CoreCoord eth_core, std::chrono::milliseconds timeout_ms) {
    return device_firmware_->wait_eth_core_training(eth_core, timeout_ms);
}

void TTDevice::wait_dram_channel_training(uint32_t dram_channel, std::chrono::milliseconds timeout_ms) {
    device_firmware_->wait_dram_channel_training(dram_channel, timeout_ms);
}

void TTDevice::wait_for_non_mmio_flush() { device_firmware_->wait_for_non_mmio_flush(); }

DeviceCommandResult TTDevice::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t> &args, std::chrono::milliseconds timeout) {
    return device_firmware_->send_device_command(msg_code, args, timeout);
}

EthTrainingStatus TTDevice::read_eth_core_training_status(CoreCoord eth_core) {
    return device_firmware_->read_eth_core_training_status(eth_core);
}

void TTDevice::set_power_state(PowerState state) { device_firmware_->set_power_state(static_cast<uint32_t>(state)); }

void TTDevice::set_clock_state(PowerState state) { device_firmware_->set_clock_state(static_cast<uint32_t>(state)); }

// --- HangDetector ---

bool TTDevice::is_bus_hung(uint32_t data_read, HangAction action) {
    if (!hang_detector_) {
        return false;
    }
    auto result = hang_detector_->is_bus_hung(data_read);
    if (!result.has_value()) {
        return false;
    }
    if (*result && action == HangAction::THROW) {
        throw std::runtime_error("Bus hang detected");
    }
    return *result;
}

bool TTDevice::is_noc_hung(NocId noc, HangAction action) {
    if (!hang_detector_) {
        return false;
    }
    auto result = hang_detector_->is_noc_hung(noc);
    if (!result.has_value()) {
        return false;
    }
    if (*result && action == HangAction::THROW) {
        throw std::runtime_error("NOC hang detected");
    }
    return *result;
}

// --- RISC Reset (composite: ArchitectureImplementation + DeviceProtocol) ---

RiscType TTDevice::get_risc_reset_state(CoreCoord core) {
    uint32_t reg_value = 0;
    device_protocol_->read_ctrl(
        &reg_value,
        translate(core),
        architecture_impl_->get_tensix_soft_reset_addr(),
        sizeof(reg_value),
        NocId::DEFAULT_NOC);
    return architecture_impl_->get_soft_reset_risc_type(reg_value);
}

void TTDevice::assert_risc_reset(CoreCoord core, RiscType selected_riscs) {
    uint32_t current = 0;
    auto xy = translate(core);
    device_protocol_->read_ctrl(
        &current, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(current), NocId::DEFAULT_NOC);
    uint32_t new_value = current | architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    device_protocol_->write_ctrl(
        &new_value, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(new_value), NocId::DEFAULT_NOC);
}

void TTDevice::deassert_risc_reset(CoreCoord core, RiscType selected_riscs, bool staggered_start) {
    uint32_t current = 0;
    auto xy = translate(core);
    device_protocol_->read_ctrl(
        &current, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(current), NocId::DEFAULT_NOC);
    uint32_t new_value = current & ~architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (staggered_start) {
        new_value |= architecture_impl_->get_soft_reset_staggered_start();
    }
    device_protocol_->write_ctrl(
        &new_value, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(new_value), NocId::DEFAULT_NOC);
}

// --- IoWindow ---

std::unique_ptr<IoWindow> TTDevice::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    return io_window_factory_->create_io_window(target, host);
}

// --- Component Getters ---

DeviceProtocol *TTDevice::get_device_protocol() { return device_protocol_.get(); }

PcieInterface *TTDevice::get_pcie_interface() { return pcie_interface_; }

JtagInterface *TTDevice::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *TTDevice::get_remote_interface() { return remote_interface_; }

FirmwareTelemetryReader *TTDevice::get_firmware_telemetry_reader() const { return firmware_telemetry_reader_.get(); }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return firmware_info_provider_.get(); }

RemoteCommunication *TTDevice::get_remote_communication() {
    return remote_interface_ ? remote_interface_->get_remote_communication() : nullptr;
}

ArchitectureImplementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

// --- Identity ---

tt::ARCH TTDevice::get_arch() const { return arch_; }

bool TTDevice::is_remote() const { return is_remote_; }

int TTDevice::get_communication_device_id() const { return device_id_; }

IODeviceType TTDevice::get_communication_device_type() const { return device_type_; }

ChipInfo TTDevice::get_chip_info() { return device_firmware_->get_chip_info(); }

FirmwareBundleVersion TTDevice::get_firmware_version() { return device_firmware_->get_firmware_version(); }

bool TTDevice::get_noc_translation_enabled() const { return device_firmware_->get_noc_translation_enabled(); }

uint64_t TTDevice::get_board_id() const {
    return firmware_info_provider_ ? firmware_info_provider_->get_board_id().value_or(0) : 0;
}

uint8_t TTDevice::get_asic_location() const {
    return firmware_info_provider_ ? firmware_info_provider_->get_asic_location().value_or(0) : 0;
}

BoardType TTDevice::get_board_type() const { return get_board_type_from_board_id(get_board_id()); }

const SocDescriptor &TTDevice::get_soc_descriptor() const { return soc_descriptor_.value(); }

// --- Clock and Thermal ---

double TTDevice::get_asic_temperature() const {
    return firmware_info_provider_ ? firmware_info_provider_->get_asic_temperature().value_or(0.0) : 0.0;
}

uint32_t TTDevice::get_clock_freq() const { return device_firmware_->get_clock_freq(); }

uint32_t TTDevice::get_max_clock_freq() const {
    return firmware_info_provider_ ? firmware_info_provider_->get_max_clock_freq().value_or(0) : 0;
}

uint32_t TTDevice::get_min_clock_freq() const { return architecture_impl_->get_min_clock_freq(); }

uint64_t TTDevice::get_refclk_counter() const {
    uint32_t high = 0, low = 0;
    tt_xy_pair arc = device_firmware_->get_arc_core();
    device_protocol_->read_ctrl(
        &high, arc, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high), NocId::DEFAULT_NOC);
    device_protocol_->read_ctrl(
        &low, arc, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low), NocId::DEFAULT_NOC);
    return (static_cast<uint64_t>(high) << 32) | low;
}

int TTDevice::get_numa_node() const { return pcie_interface_ ? pcie_interface_->get_numa_node() : -1; }

/* static */ void TTDevice::set_sigbus_safe_handler(bool) {}

// --- HangDetector stub implementations ---

std::optional<bool> HangDetector::is_bus_hung(uint32_t data_read) {
    if (data_read != HANG_READ_VALUE) {
        return false;
    }
    uint32_t check = read_hang_check_reg_via_bar();
    return check == HANG_READ_VALUE;
}

std::optional<bool> HangDetector::is_noc_hung(NocId noc) {
    uint32_t check = read_hang_check_reg_via_noc(noc);
    return check == HANG_READ_VALUE;
}

}  // namespace tt::umd
