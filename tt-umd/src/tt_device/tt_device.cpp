// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device.hpp"

#include <string>
#include <utility>

#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device_model/blackhole_tt_device_model.hpp"

namespace tt::umd {

std::unique_ptr<TTDevice> TTDevice::create(
    int device_number,
    IODeviceType device_type,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    switch (device_type) {
        case IODeviceType::PCIe:
            break;
        case IODeviceType::JTAG:
            UMD_THROW(error::RuntimeError, "JTAG devices are not supported.");
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Cannot create a TTDevice for I/O device type {}.", DeviceTypeToString.at(device_type)));
    }

    const tt::ARCH arch = PCIDevice::read_device_info("/dev/tenstorrent/" + std::to_string(device_number)).get_arch();
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_THROW(error::RuntimeError, fmt::format("PCIe device {} has an unsupported architecture.", device_number));
    }
    return std::unique_ptr<TTDevice>(new TTDevice(std::make_unique<BlackholeTTDeviceModel>(
        device_number, static_cast<IoSafety>(use_safe_api), soc_arch_descriptor)));
}

std::unique_ptr<TTDevice> TTDevice::create(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    UMD_THROW(error::RuntimeError, "Remote devices are not supported.");
}

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) {}

ArchitectureImplementation *TTDevice::get_architecture_implementation() { return model_->get_architecture_impl(); }

PCIDevice *TTDevice::get_pci_device() { return model_->get_pci_device(); }

DeviceProtocol *TTDevice::get_device_protocol() { return model_->get_device_protocol(); }

PcieInterface *TTDevice::get_pcie_interface() { return model_->get_pcie_interface(); }

JtagInterface *TTDevice::get_jtag_interface() { return model_->get_jtag_interface(); }

RemoteInterface *TTDevice::get_remote_interface() { return model_->get_remote_interface(); }

DmaInterface *TTDevice::get_dma_interface() { return model_->get_dma_interface(); }

RiscReset *TTDevice::get_risc_reset() { return model_->get_risc_reset(); }

tt::ARCH TTDevice::get_arch() const { return model_->get_architecture_impl()->get_architecture(); }

DeviceFirmware *TTDevice::get_device_firmware() const { return model_->get_device_firmware(); }

FirmwareTelemetryReader *TTDevice::get_firmware_telemetry_reader() const {
    return model_->get_firmware_telemetry_reader();
}

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return model_->get_firmware_info_provider(); }

uint32_t TTDevice::get_min_clock_freq() { return model_->get_architecture_impl()->get_min_clock_freq(); }

void TTDevice::advance_device_execution() {}

bool TTDevice::is_remote() { return get_remote_interface() != nullptr; }

int TTDevice::get_communication_device_id() const { return model_->get_device_protocol()->get_mmio_id(); }

IODeviceType TTDevice::get_communication_device_type() const {
    if (model_->get_pcie_interface() != nullptr) {
        return IODeviceType::PCIe;
    }
    if (model_->get_jtag_interface() != nullptr) {
        return IODeviceType::JTAG;
    }
    return IODeviceType::UNDEFINED;
}

void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) {
    SiliconTlbWindow::set_sigbus_safe_handler(set_safe_handler);
}

RemoteCommunication *TTDevice::get_remote_communication() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_remote_communication is not implemented yet");
}

bool TTDevice::is_pcie_hung(uint32_t data_read, HangAction action) {
    UMD_THROW(error::RuntimeError, "TTDevice::is_pcie_hung is not implemented yet");
}

bool TTDevice::is_noc_hung(NocId noc, HangAction action) {
    UMD_THROW(error::RuntimeError, "TTDevice::is_noc_hung is not implemented yet");
}

void TTDevice::read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::read_from_device is not implemented yet");
}

void TTDevice::write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::write_to_device is not implemented yet");
}

void TTDevice::read_from_device_reg(
    void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_THROW(error::RuntimeError, "TTDevice::read_from_device_reg is not implemented yet");
}

void TTDevice::write_to_device_reg(
    const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_THROW(error::RuntimeError, "TTDevice::write_to_device_reg is not implemented yet");
}

void TTDevice::dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_write is not implemented yet");
}

void TTDevice::dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_read is not implemented yet");
}

void TTDevice::dma_write_to_core_range(
    const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_write_to_core_range is not implemented yet");
}

void TTDevice::dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_read_zero_copy is not implemented yet");
}

void TTDevice::dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_write_zero_copy is not implemented yet");
}

bool TTDevice::dma_write_zero_copy_start(
    uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_write_zero_copy_start is not implemented yet");
}

bool TTDevice::dma_read_zero_copy_start(
    uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_read_zero_copy_start is not implemented yet");
}

DmaTransferState TTDevice::dma_write_zero_copy_check() {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_write_zero_copy_check is not implemented yet");
}

DmaTransferState TTDevice::dma_read_zero_copy_check() {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_read_zero_copy_check is not implemented yet");
}

size_t TTDevice::dma_zero_copy_max_size(uint64_t device_addr) {
    UMD_THROW(error::RuntimeError, "TTDevice::dma_zero_copy_max_size is not implemented yet");
}

void TTDevice::noc_multicast_write(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::noc_multicast_write is not implemented yet");
}

void TTDevice::noc_multicast_write(const void *src, size_t size, uint64_t addr, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::noc_multicast_write is not implemented yet");
}

ChipInfo TTDevice::get_chip_info() { UMD_THROW(error::RuntimeError, "TTDevice::get_chip_info is not implemented yet"); }

FirmwareBundleVersion TTDevice::get_firmware_version() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_firmware_version is not implemented yet");
}

std::chrono::milliseconds TTDevice::wait_eth_core_training(
    CoreCoord eth_core, const std::chrono::milliseconds timeout_ms) {
    UMD_THROW(error::RuntimeError, "TTDevice::wait_eth_core_training is not implemented yet");
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    UMD_THROW(error::RuntimeError, "TTDevice::wait_dram_channel_training is not implemented yet");
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) {
    UMD_THROW(error::RuntimeError, "TTDevice::bar_write32 is not implemented yet");
}

uint32_t TTDevice::bar_read32(uint32_t addr) {
    UMD_THROW(error::RuntimeError, "TTDevice::bar_read32 is not implemented yet");
}

tt_xy_pair TTDevice::get_arc_core() const {
    UMD_THROW(error::RuntimeError, "TTDevice::get_arc_core is not implemented yet");
}

tt_xy_pair TTDevice::get_arc_core(const NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "TTDevice::get_arc_core is not implemented yet");
}

void TTDevice::set_power_state(PowerState state, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::set_power_state is not implemented yet");
}

void TTDevice::set_clock_state(ClockState state, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::set_clock_state is not implemented yet");
}

uint32_t TTDevice::get_clock() { UMD_THROW(error::RuntimeError, "TTDevice::get_clock is not implemented yet"); }

uint32_t TTDevice::get_max_clock_freq() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_max_clock_freq is not implemented yet");
}

uint64_t TTDevice::get_board_id() { UMD_THROW(error::RuntimeError, "TTDevice::get_board_id is not implemented yet"); }

uint8_t TTDevice::get_asic_location() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_asic_location is not implemented yet");
}

BoardType TTDevice::get_board_type() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_board_type is not implemented yet");
}

bool TTDevice::get_noc_translation_enabled() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_noc_translation_enabled is not implemented yet");
}

double TTDevice::get_asic_temperature() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_asic_temperature is not implemented yet");
}

void TTDevice::wait_for_non_mmio_flush() {
    UMD_THROW(error::RuntimeError, "TTDevice::wait_for_non_mmio_flush is not implemented yet");
}

void TTDevice::init_tt_device(std::chrono::milliseconds timeout_ms) {
    UMD_THROW(error::RuntimeError, "TTDevice::init_tt_device is not implemented yet");
}

uint64_t TTDevice::get_refclk_counter() {
    UMD_THROW(error::RuntimeError, "TTDevice::get_refclk_counter is not implemented yet");
}

uint32_t TTDevice::get_risc_reset_state(CoreCoord core) {
    UMD_THROW(error::RuntimeError, "TTDevice::get_risc_reset_state is not implemented yet");
}

void TTDevice::set_risc_reset_state(CoreCoord core, const uint32_t risc_flags) {
    UMD_THROW(error::RuntimeError, "TTDevice::set_risc_reset_state is not implemented yet");
}

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::assert_risc_reset is not implemented yet");
}

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::deassert_risc_reset is not implemented yet");
}

std::unique_ptr<TlbWindow> TTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    UMD_THROW(error::RuntimeError, "TTDevice::get_io_window is not implemented yet");
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    UMD_THROW(error::RuntimeError, "TTDevice::create_io_window is not implemented yet");
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host, IoOrdering ordering) {
    UMD_THROW(error::RuntimeError, "TTDevice::create_io_window is not implemented yet");
}

int TTDevice::export_dmabuf(CoreCoord core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::export_dmabuf is not implemented yet");
}

EthTrainingStatus TTDevice::read_eth_core_training_status(CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TTDevice::read_eth_core_training_status is not implemented yet");
}

const SocDescriptor &TTDevice::get_soc_descriptor() const {
    UMD_THROW(error::RuntimeError, "TTDevice::get_soc_descriptor is not implemented yet");
}

void TTDevice::multicast_write_via_unicast(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "TTDevice::multicast_write_via_unicast is not implemented yet");
}

void TTDevice::construct_soc_descriptor(const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    UMD_THROW(error::RuntimeError, "TTDevice::construct_soc_descriptor is not implemented yet");
}

void TTDevice::set_soc_descriptor(const SocDescriptor &soc_descriptor) {
    UMD_THROW(error::RuntimeError, "TTDevice::set_soc_descriptor is not implemented yet");
}

void TTDevice::wire_hang_detector() {
    UMD_THROW(error::RuntimeError, "TTDevice::wire_hang_detector is not implemented yet");
}

std::function<bool(NocId)> TTDevice::make_io_timeout_hang_check() {
    UMD_THROW(error::RuntimeError, "TTDevice::make_io_timeout_hang_check is not implemented yet");
}

xy_pair TTDevice::resolve_coordinate(CoreCoord core, NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "TTDevice::resolve_coordinate is not implemented yet");
}

std::pair<xy_pair, uint64_t> TTDevice::resolve_target(CoreCoord core, uint64_t addr, size_t size, NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "TTDevice::resolve_target is not implemented yet");
}

}  // namespace tt::umd
