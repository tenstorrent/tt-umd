// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "common/unsupported.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/tt_device/remote_communication.hpp"

namespace tt::umd {

std::unique_ptr<TTDevice> TTDevice::create(
    int device_number,
    IODeviceType device_type,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<TTDevice> TTDevice::create(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    UMD_NOT_SUPPORTED("A remote TTDevice");
}

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) {}

ArchitectureImplementation *TTDevice::get_architecture_implementation() { UMD_NOT_IMPLEMENTED(); }

PCIDevice *TTDevice::get_pci_device() { UMD_NOT_IMPLEMENTED(); }

RemoteCommunication *TTDevice::get_remote_communication() { UMD_NOT_IMPLEMENTED(); }

DeviceProtocol *TTDevice::get_device_protocol() { UMD_NOT_IMPLEMENTED(); }

PcieInterface *TTDevice::get_pcie_interface() { UMD_NOT_IMPLEMENTED(); }

JtagInterface *TTDevice::get_jtag_interface() { UMD_NOT_IMPLEMENTED(); }

RemoteInterface *TTDevice::get_remote_interface() { UMD_NOT_IMPLEMENTED(); }

tt::ARCH TTDevice::get_arch() const { UMD_NOT_IMPLEMENTED(); }

bool TTDevice::is_pcie_hung(uint32_t data_read, HangAction action) { UMD_NOT_IMPLEMENTED(); }

bool TTDevice::is_noc_hung(NocId noc, HangAction action) { UMD_NOT_IMPLEMENTED(); }

void TTDevice::read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::read_from_device_reg(
    void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::write_to_device_reg(
    const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::dma_write_to_core_range(
    const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

bool TTDevice::dma_write_zero_copy_start(
    uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

bool TTDevice::dma_read_zero_copy_start(
    uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

DmaTransferState TTDevice::dma_write_zero_copy_check() { UMD_NOT_IMPLEMENTED(); }

DmaTransferState TTDevice::dma_read_zero_copy_check() { UMD_NOT_IMPLEMENTED(); }

size_t TTDevice::dma_zero_copy_max_size(uint64_t device_addr) { UMD_NOT_IMPLEMENTED(); }

void TTDevice::noc_multicast_write(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::noc_multicast_write(const void *src, size_t size, uint64_t addr, NocId noc_id) { UMD_NOT_IMPLEMENTED(); }

ChipInfo TTDevice::get_chip_info() { UMD_NOT_IMPLEMENTED(); }

FirmwareBundleVersion TTDevice::get_firmware_version() { UMD_NOT_IMPLEMENTED(); }

DeviceFirmware *TTDevice::get_device_firmware() const { UMD_NOT_IMPLEMENTED(); }

std::chrono::milliseconds TTDevice::wait_eth_core_training(
    CoreCoord eth_core, const std::chrono::milliseconds timeout_ms) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) { UMD_NOT_IMPLEMENTED(); }

uint32_t TTDevice::bar_read32(uint32_t addr) { UMD_NOT_IMPLEMENTED(); }

FirmwareTelemetryReader *TTDevice::get_firmware_telemetry_reader() const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair TTDevice::get_arc_core() const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair TTDevice::get_arc_core(const NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { UMD_NOT_IMPLEMENTED(); }

void TTDevice::set_power_state(PowerState state, NocId noc_id) { UMD_NOT_IMPLEMENTED(); }

void TTDevice::set_clock_state(ClockState state, NocId noc_id) { UMD_NOT_IMPLEMENTED(); }

uint32_t TTDevice::get_clock() { UMD_NOT_IMPLEMENTED(); }

uint32_t TTDevice::get_max_clock_freq() { UMD_NOT_IMPLEMENTED(); }

uint32_t TTDevice::get_min_clock_freq() { UMD_NOT_IMPLEMENTED(); }

void TTDevice::advance_device_execution() {}

uint64_t TTDevice::get_board_id() { UMD_NOT_IMPLEMENTED(); }

uint8_t TTDevice::get_asic_location() { UMD_NOT_IMPLEMENTED(); }

BoardType TTDevice::get_board_type() { UMD_NOT_IMPLEMENTED(); }

bool TTDevice::get_noc_translation_enabled() { UMD_NOT_IMPLEMENTED(); }

double TTDevice::get_asic_temperature() { UMD_NOT_IMPLEMENTED(); }

void TTDevice::wait_for_non_mmio_flush() { UMD_NOT_IMPLEMENTED(); }

bool TTDevice::is_remote() { UMD_NOT_IMPLEMENTED(); }

void TTDevice::init_tt_device(std::chrono::milliseconds timeout_ms) { UMD_NOT_IMPLEMENTED(); }

uint64_t TTDevice::get_refclk_counter() { UMD_NOT_IMPLEMENTED(); }

int TTDevice::get_communication_device_id() const { UMD_NOT_IMPLEMENTED(); }

IODeviceType TTDevice::get_communication_device_type() const { UMD_NOT_IMPLEMENTED(); }

uint32_t TTDevice::get_risc_reset_state(CoreCoord core) { UMD_NOT_IMPLEMENTED(); }

void TTDevice::set_risc_reset_state(CoreCoord core, const uint32_t risc_flags) { UMD_NOT_IMPLEMENTED(); }

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc_id) { UMD_NOT_IMPLEMENTED(); }

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<TlbWindow> TTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host, IoOrdering ordering) {
    UMD_NOT_IMPLEMENTED();
}

int TTDevice::export_dmabuf(CoreCoord core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) { UMD_NOT_IMPLEMENTED(); }

EthTrainingStatus TTDevice::read_eth_core_training_status(CoreCoord eth_core) { UMD_NOT_IMPLEMENTED(); }

const SocDescriptor &TTDevice::get_soc_descriptor() const { UMD_NOT_IMPLEMENTED(); }

void TTDevice::multicast_write_via_unicast(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::construct_soc_descriptor(const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    UMD_NOT_IMPLEMENTED();
}

void TTDevice::set_soc_descriptor(const SocDescriptor &soc_descriptor) { UMD_NOT_IMPLEMENTED(); }

void TTDevice::wire_hang_detector() { UMD_NOT_IMPLEMENTED(); }

std::function<bool(NocId)> TTDevice::make_io_timeout_hang_check() { UMD_NOT_IMPLEMENTED(); }

xy_pair TTDevice::resolve_coordinate(CoreCoord core, NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::pair<xy_pair, uint64_t> TTDevice::resolve_target(CoreCoord core, uint64_t addr, size_t size, NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

DmaInterface *TTDevice::get_dma_interface() { UMD_NOT_IMPLEMENTED(); }

RiscReset *TTDevice::get_risc_reset() { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
