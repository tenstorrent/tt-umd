// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device_doxy.hpp"

#include "tt_device_model_doxy.hpp"
#include "types/tt_core_coordinates_doxy.hpp"

namespace tt::umd {

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) {
    // Mandatory components.
    device_protocol_ = model_->get_device_protocol();
    device_firmware_ = model_->get_device_firmware();
    architecture_impl_ = model_->get_architecture_impl();

    // Optional components.
    hang_detector_ = model_->get_hang_detector();
    dma_interface_ = model_->get_dma_interface();

    // Cache interface pointers from the protocol.
    pcie_interface_ = model_->get_pcie_interface();
    jtag_interface_ = model_->get_jtag_interface();
    remote_interface_ = model_->get_remote_interface();

    is_remote_ = remote_interface_ != nullptr;
}

TTDevice::~TTDevice() = default;

void TTDevice::init_device(std::chrono::milliseconds timeout_ms, NocId noc) {
    device_firmware_->init_firmware(timeout_ms, noc);

    firmware_telemetry_reader_ = model_->get_firmware_telemetry_reader();
    firmware_info_provider_ = model_->get_firmware_info_provider();

    soc_descriptor_.emplace(
        std::make_shared<SocArchDescriptor>(architecture_impl_->get_architecture()),
        device_firmware_->get_chip_info(noc));
}

tt_xy_pair TTDevice::translate(CoreCoord core) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return xy_pair(core.x, core.y);
    }
    return get_soc_descriptor().translate_chip_coord_to_translated(core);
}

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
    auto res = device_protocol_->write_to_core_range(src, translate(core_start), translate(core_end), addr, size, noc);
    if (!res) {
        // Fallback: iterate active (non-harvested) cores in the range via SocDescriptor
        // and unicast write_data to each one individually.
    }
}

void TTDevice::write_to_core_range(const void *src, size_t size, uint64_t addr, NocId noc) {
    // TODO: get broadcast grid from SocDescriptor
    // (void)device_protocol_->write_to_core_range(src, broadcast_start, broadcast_end, addr, size, noc);
}

void TTDevice::dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc) {
    bool success = dma_interface_ && dma_interface_->dma_read(dst, src_addr, size, translate(core), noc);
    if (success) {
        return;
    }
    device_protocol_->read_data(dst, translate(core), src_addr, size, noc);
}

void TTDevice::dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc) {
    bool success = dma_interface_ && dma_interface_->dma_write(src, dst_addr, size, translate(core), noc);
    if (success) {
        return;
    }
    device_protocol_->write_data(src, translate(core), dst_addr, size, noc);
}

void TTDevice::dma_write_to_core_range(
    const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc) {
    bool success = dma_interface_ && dma_interface_->dma_multicast_write(
                                         src, dst_addr, size, translate(core_start), translate(core_end), noc);
    if (success) {
        return;
    }
    (void)device_protocol_->write_to_core_range(src, translate(core_start), translate(core_end), dst_addr, size, noc);
}

void TTDevice::dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc) {
    bool success = dma_interface_ && dma_interface_->dma_read_zero_copy(dst_iova, src_addr, size, translate(core), noc);
    if (success) {
        return;
    }
    // throw: zero-copy requires a functional DMA interface
}

void TTDevice::dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc) {
    bool success =
        dma_interface_ && dma_interface_->dma_write_zero_copy(src_iova, dst_addr, size, translate(core), noc);
    if (success) {
        return;
    }
    // throw: zero-copy requires a functional DMA interface
}

void TTDevice::dma_write_to_core_range_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc) {
    bool success = dma_interface_ && dma_interface_->dma_multicast_write_zero_copy(
                                         src_iova, dst_addr, size, translate(core_start), translate(core_end), noc);
    if (success) {
        return;
    }
    // throw: zero-copy requires a functional DMA interface
}

// --- DeviceFirmware ---

void TTDevice::init_firmware(const std::chrono::milliseconds timeout_ms, NocId noc) {
    device_firmware_->init_firmware(timeout_ms, noc);
}

bool TTDevice::wait_eth_core_training(const CoreCoord eth_core, const std::chrono::milliseconds timeout_ms, NocId noc) {
    return device_firmware_->wait_eth_core_training(translate(eth_core), timeout_ms, noc);
}

void TTDevice::wait_dram_channel_training(
    const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms, NocId noc) {
    device_firmware_->wait_dram_channel_training(dram_channel, timeout_ms, noc);
}

void TTDevice::wait_for_non_mmio_flush(NocId noc) { remote_interface_->wait_for_non_mmio_flush(); }

DeviceCommandResult TTDevice::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t> &args, std::chrono::milliseconds timeout, NocId noc) {
    return device_firmware_->send_device_command(msg_code, args, timeout, noc);
}

EthTrainingStatus TTDevice::get_eth_core_training_status(CoreCoord eth_core, NocId noc) {
    return device_firmware_->get_eth_core_training_status(translate(eth_core), noc);
}

void TTDevice::set_power_state(PowerState state, NocId noc) {
    device_firmware_->set_power_state(static_cast<uint32_t>(state), noc);
}

void TTDevice::set_clock_state(PowerState state, NocId noc) {
    device_firmware_->set_clock_state(static_cast<uint32_t>(state), noc);
}

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

RiscType TTDevice::get_risc_reset_state(CoreCoord core, NocId noc) {
    uint32_t reg_value = 0;
    device_protocol_->read_ctrl(
        &reg_value, translate(core), architecture_impl_->get_tensix_soft_reset_addr(), sizeof(reg_value), noc);
    return architecture_impl_->get_soft_reset_risc_type(reg_value);
}

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc) {
    uint32_t current = 0;
    auto xy = translate(core);
    device_protocol_->read_ctrl(&current, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(current), noc);
    uint32_t new_value = current | architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    device_protocol_->write_ctrl(
        &new_value, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(new_value), noc);
}

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start, NocId noc) {
    uint32_t current = 0;
    auto xy = translate(core);
    device_protocol_->read_ctrl(&current, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(current), noc);
    uint32_t new_value = current & ~architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (staggered_start) {
        new_value |= architecture_impl_->get_soft_reset_staggered_start();
    }
    device_protocol_->write_ctrl(
        &new_value, xy, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(new_value), noc);
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    return model_->create_io_window(target, host);
}

DeviceProtocol *TTDevice::get_device_protocol() { return device_protocol_; }

PcieInterface *TTDevice::get_pcie_interface() { return pcie_interface_; }

JtagInterface *TTDevice::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *TTDevice::get_remote_interface() { return remote_interface_; }

FirmwareTelemetryReader *TTDevice::get_firmware_telemetry_reader() const { return firmware_telemetry_reader_; }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return firmware_info_provider_; }

RemoteCommunication *TTDevice::get_remote_communication() {
    return remote_interface_ ? remote_interface_->get_remote_communication() : nullptr;
}

ArchitectureImplementation *TTDevice::get_architecture_implementation() { return architecture_impl_; }

ARCH TTDevice::get_arch() const { return architecture_impl_->get_architecture(); }

bool TTDevice::is_remote() const { return is_remote_; }

int TTDevice::get_communication_device_id() const { return device_protocol_->get_mmio_id(); }

IODeviceType TTDevice::get_communication_device_type() const {
    if (pcie_interface_) {
        return IODeviceType::PCIe;
    }
    if (jtag_interface_) {
        return IODeviceType::JTAG;
    }
    return IODeviceType::UNDEFINED;
}

ChipInfo TTDevice::get_chip_info(NocId noc) { return device_firmware_->get_chip_info(noc); }

FirmwareBundleVersion TTDevice::get_firmware_version(NocId noc) {
    return firmware_info_provider_->get_firmware_version(noc);
}

bool TTDevice::get_noc_translation_enabled() const { return device_firmware_->get_noc_translation_enabled(); }

uint64_t TTDevice::get_board_id() const {
    return firmware_info_provider_ ? firmware_info_provider_->get_board_id().value_or(0) : 0;
}

uint8_t TTDevice::get_asic_location() const {
    return firmware_info_provider_ ? firmware_info_provider_->get_asic_location().value_or(0) : 0;
}

BoardType TTDevice::get_board_type() const { return get_board_type_from_board_id(get_board_id()); }

double TTDevice::get_asic_temperature(NocId noc) const {
    return firmware_info_provider_ ? firmware_info_provider_->get_asic_temperature(noc).value_or(0.0) : 0.0;
}

uint32_t TTDevice::get_clock_freq(NocId noc) const {
    if (firmware_info_provider_->get_clock_freq(noc).has_value()) {
        return firmware_info_provider_->get_clock_freq(noc).value();
    }
    return 0;
}

uint32_t TTDevice::get_max_clock_freq(NocId noc) const {
    return firmware_info_provider_ ? firmware_info_provider_->get_max_clock_freq(noc).value_or(0) : 0;
}

uint32_t TTDevice::get_min_clock_freq([[maybe_unused]] NocId noc) const {
    return architecture_impl_->get_min_clock_freq();
}

uint64_t TTDevice::get_refclk_counter(NocId noc) const {
    uint32_t high = 0;
    uint32_t low = 0;
    tt_xy_pair fw_core = device_firmware_->get_firmware_noc_coord();
    device_protocol_->read_ctrl(
        &high, fw_core, architecture_impl_->get_reset_unit_refclk_high_offset(), sizeof(high), noc);
    device_protocol_->read_ctrl(
        &low, fw_core, architecture_impl_->get_reset_unit_refclk_low_offset(), sizeof(low), noc);
    return (static_cast<uint64_t>(high) << 32) | low;
}

int TTDevice::get_numa_node() const { return pcie_interface_ ? pcie_interface_->get_numa_node() : -1; }

const SocDescriptor &TTDevice::get_soc_descriptor() const { return soc_descriptor_.value(); }

/* static */ void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) {}

}  // namespace tt::umd
