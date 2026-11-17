// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "common/utils.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/tt_device/reset/risc_reset.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device/hang_detection/hang_detector_implementation.hpp"
#include "tt_device/protocol/dma_interface.hpp"
#include "tt_device/protocol/pcie_protocol.hpp"
#include "tt_device_model/blackhole_tt_device_model.hpp"

namespace tt::umd {

namespace {

// Firmware-backed state (chip info, the SoC descriptor, the info provider and telemetry reader) exists only once
// init_tt_device() has run.
void throw_if_uninitialized(const TTDevice &tt_device, bool initialized) {
    if (!initialized) {
        UMD_THROW(error::UninitializedDeviceError, tt_device);
    }
}

RiscReset *require_risc_reset(RiscReset *risc_reset) {
    if (risc_reset == nullptr) {
        UMD_THROW(error::RuntimeError, "RISC reset is not available on this device.");
    }
    return risc_reset;
}

TlbMapping to_tlb_mapping(HostMemoryCaching caching) {
    return caching == HostMemoryCaching::UC ? TlbMapping::UC : TlbMapping::WC;
}

[[noreturn]] void throw_dma_unavailable(const TTDevice &tt_device, const std::string &what) {
    UMD_THROW(
        error::RuntimeError,
        fmt::format("{} is not available on device {}.", what, tt_device.get_communication_device_id()));
}

DmaInterface *require_dma(const TTDevice &tt_device, DmaInterface *dma) {
    if (dma == nullptr) {
        throw_dma_unavailable(tt_device, "PCIe DMA");
    }
    return dma;
}

}  // namespace

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

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) { wire_hang_detector(); }

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
    throw_if_uninitialized(*this, soc_descriptor_.has_value());
    return model_->get_firmware_telemetry_reader();
}

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const {
    throw_if_uninitialized(*this, soc_descriptor_.has_value());
    return model_->get_firmware_info_provider();
}

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
    RemoteInterface *remote_interface = get_remote_interface();
    return remote_interface != nullptr ? remote_interface->get_remote_communication() : nullptr;
}

bool TTDevice::is_pcie_hung(uint32_t data_read, HangAction action) {
    HangDetector *hang_detector = model_->get_hang_detector();
    if (hang_detector == nullptr) {
        return false;
    }
    const std::optional<bool> hung = hang_detector->is_bus_hung(data_read);
    if (!hung.has_value()) {
        return false;
    }
    if (*hung && action == HangAction::THROW) {
        UMD_THROW(error::PcieHangError, *this, data_read);
    }
    return *hung;
}

bool TTDevice::is_noc_hung(NocId noc, HangAction action) {
    HangDetector *hang_detector = model_->get_hang_detector();
    if (hang_detector == nullptr) {
        return false;
    }
    const std::optional<bool> hung = hang_detector->is_noc_hung(noc);
    if (!hung.has_value()) {
        return false;
    }
    if (*hung && action == HangAction::THROW) {
        UMD_THROW(error::NocHangError, *this, noc);
    }
    return *hung;
}

void TTDevice::read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    get_device_protocol()->read_data(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id);
}

void TTDevice::write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    get_device_protocol()->write_data(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id);
}

void TTDevice::read_from_device_reg(
    void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    get_device_protocol()->read_ctrl(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id, width);
}

void TTDevice::write_to_device_reg(
    const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    get_device_protocol()->write_ctrl(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id, width);
}

void TTDevice::dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    DmaInterface *dma = get_dma_interface();
    if (dma == nullptr || !dma->dma_write(src, dst_addr, size, resolve_coordinate(core, noc_id), noc_id)) {
        write_to_device(src, core, dst_addr, size, noc_id);
    }
}

void TTDevice::dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    DmaInterface *dma = get_dma_interface();
    if (dma == nullptr || !dma->dma_read(dst, src_addr, size, resolve_coordinate(core, noc_id), noc_id)) {
        read_from_device(dst, core, src_addr, size, noc_id);
    }
}

void TTDevice::dma_write_to_core_range(
    const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc_id) {
    DmaInterface *dma = get_dma_interface();
    if (dma == nullptr || !dma->dma_multicast_write(
                              src,
                              dst_addr,
                              size,
                              resolve_coordinate(core_start, noc_id),
                              resolve_coordinate(core_end, noc_id),
                              noc_id)) {
        // Falls back the way a NOC multicast does, down to unicast writes, so the data always lands.
        noc_multicast_write(src, size, core_start, core_end, dst_addr, noc_id);
    }
}

void TTDevice::dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    DmaInterface *dma = get_dma_interface();
    // Zero-copy has no fallback: the caller's memory is reachable only by the DMA engine.
    if (dma == nullptr ||
        !dma->dma_read_zero_copy(dst_iova, src_addr, size, resolve_coordinate(core, noc_id), noc_id)) {
        throw_dma_unavailable(*this, "Device-to-host zero-copy DMA");
    }
}

void TTDevice::dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    DmaInterface *dma = get_dma_interface();
    if (dma == nullptr ||
        !dma->dma_write_zero_copy(src_iova, dst_addr, size, resolve_coordinate(core, noc_id), noc_id)) {
        throw_dma_unavailable(*this, "Host-to-device zero-copy DMA");
    }
}

bool TTDevice::dma_write_zero_copy_start(
    uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    return require_dma(*this, get_dma_interface())
        ->dma_write_zero_copy_start(src_iova, dst_addr, size, resolve_coordinate(core, noc_id), noc_id);
}

bool TTDevice::dma_read_zero_copy_start(
    uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    return require_dma(*this, get_dma_interface())
        ->dma_read_zero_copy_start(dst_iova, src_addr, size, resolve_coordinate(core, noc_id), noc_id);
}

DmaTransferState TTDevice::dma_write_zero_copy_check() {
    DmaInterface *dma = get_dma_interface();
    return dma != nullptr ? dma->dma_write_zero_copy_check() : DmaTransferState::IDLE;
}

DmaTransferState TTDevice::dma_read_zero_copy_check() {
    DmaInterface *dma = get_dma_interface();
    return dma != nullptr ? dma->dma_read_zero_copy_check() : DmaTransferState::IDLE;
}

size_t TTDevice::dma_zero_copy_max_size(uint64_t device_addr) {
    return require_dma(*this, get_dma_interface())->dma_zero_copy_max_size(device_addr);
}

void TTDevice::noc_multicast_write(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    const bool multicast_done = get_device_protocol()->write_to_core_range(
        src, resolve_coordinate(core_start, noc_id), resolve_coordinate(core_end, noc_id), addr, size, noc_id);
    if (!multicast_done) {
        multicast_write_via_unicast(src, size, core_start, core_end, addr, noc_id);
    }
}

void TTDevice::noc_multicast_write(const void *src, size_t size, uint64_t addr, NocId noc_id) {
    const SocDescriptor &soc_descriptor = get_soc_descriptor();
    const std::vector<CoreCoord> tensix_cores = soc_descriptor.get_cores(CoreType::TENSIX);

    // The rectangle spanning every usable Tensix core also spans non-Tensix columns and harvested rows or columns;
    // firmware opts those tiles out of broadcasts, so only usable Tensix cores receive the data.
    tt_xy_pair grid_start = soc_descriptor.translate_chip_coord_to_translated(tensix_cores.front(), noc_id);
    tt_xy_pair grid_end = grid_start;
    for (const CoreCoord &core : tensix_cores) {
        const tt_xy_pair translated = soc_descriptor.translate_chip_coord_to_translated(core, noc_id);
        grid_start = {std::min(grid_start.x, translated.x), std::min(grid_start.y, translated.y)};
        grid_end = {std::max(grid_end.x, translated.x), std::max(grid_end.y, translated.y)};
    }
    // Multicast corners are named as NOC0 sees the grid. Untranslated NOC1 coordinates are mirrored, so their
    // smallest corner is NOC0's lower-right one.
    if (noc_id == NocId::NOC1 && !soc_descriptor.noc_translation_enabled) {
        std::swap(grid_start, grid_end);
    }

    if (!get_device_protocol()->write_to_core_range(src, grid_start, grid_end, addr, size, noc_id)) {
        for (const CoreCoord &core : tensix_cores) {
            write_to_device(src, core, addr, size, noc_id);
        }
    }
}

ChipInfo TTDevice::get_chip_info() {
    throw_if_uninitialized(*this, soc_descriptor_.has_value());
    return get_device_firmware()->get_chip_info(get_selected_noc_id());
}

FirmwareBundleVersion TTDevice::get_firmware_version() {
    return get_firmware_info_provider()->get_firmware_version(get_selected_noc_id());
}

std::chrono::milliseconds TTDevice::wait_eth_core_training(
    CoreCoord eth_core, const std::chrono::milliseconds timeout_ms) {
    const NocId noc_id = get_selected_noc_id();
    const auto start = std::chrono::steady_clock::now();
    const bool trained =
        get_device_firmware()->wait_eth_core_training(resolve_coordinate(eth_core, noc_id), timeout_ms, noc_id);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    if (!trained) {
        // An untrained link only loses that Ethernet connection; the chip itself stays usable.
        log_warning(LogUMD, "ETH core {} did not finish training within {} ms.", eth_core.str(), timeout_ms.count());
    }
    return elapsed;
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    if (!get_device_firmware()->wait_dram_channel_training(dram_channel, timeout_ms, get_selected_noc_id())) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("DRAM channel {} did not finish training within {} ms.", dram_channel, timeout_ms.count()));
    }
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) {
    PcieInterface *pcie_interface = get_pcie_interface();
    if (pcie_interface == nullptr) {
        UMD_THROW(error::RuntimeError, "BAR access needs a PCIe device.");
    }
    pcie_interface->bar_write32(addr, data);
}

uint32_t TTDevice::bar_read32(uint32_t addr) {
    PcieInterface *pcie_interface = get_pcie_interface();
    if (pcie_interface == nullptr) {
        UMD_THROW(error::RuntimeError, "BAR access needs a PCIe device.");
    }
    return pcie_interface->bar_read32(addr);
}

tt_xy_pair TTDevice::get_arc_core() const { return get_arc_core(get_selected_noc_id()); }

tt_xy_pair TTDevice::get_arc_core(const NocId noc_id) const {
    return get_device_firmware()->get_firmware_noc_coord(noc_id);
}

void TTDevice::set_power_state(PowerState state, NocId /*noc_id*/) {
    // Power is voted through the KMD, which combines the votes of every process holding the device. Devices the KMD
    // does not manage (JTAG, remote) request no power of their own.
    PcieInterface *pcie_interface = get_pcie_interface();
    if (pcie_interface == nullptr) {
        return;
    }
    pcie_interface->set_power_state(
        state == PowerState::BUSY ? ::tt::umd::PowerState::HIGH : ::tt::umd::PowerState::LOW);
}

void TTDevice::set_clock_state(ClockState state, NocId noc_id) {
    get_device_firmware()->set_clock_state(state, noc_id);
}

uint32_t TTDevice::get_clock() {
    const std::optional<uint32_t> aiclk = get_firmware_info_provider()->get_aiclk(get_selected_noc_id());
    if (!aiclk.has_value()) {
        UMD_THROW(error::RuntimeError, "The firmware publishes no AICLK telemetry.");
    }
    return *aiclk;
}

uint32_t TTDevice::get_max_clock_freq() {
    return get_firmware_info_provider()->get_max_clock_freq(get_selected_noc_id()).value_or(0);
}

uint64_t TTDevice::get_board_id() {
    return get_firmware_info_provider()->get_board_id(get_selected_noc_id()).value_or(0);
}

uint8_t TTDevice::get_asic_location() {
    return get_firmware_info_provider()->get_asic_location(get_selected_noc_id()).value_or(0);
}

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

bool TTDevice::get_noc_translation_enabled() {
    return get_device_firmware()->get_noc_translation_enabled(get_selected_noc_id());
}

double TTDevice::get_asic_temperature() {
    return get_firmware_info_provider()->get_asic_temperature(get_selected_noc_id()).value_or(0.0);
}

void TTDevice::wait_for_non_mmio_flush() {
    RemoteInterface *remote_interface = get_remote_interface();
    if (remote_interface != nullptr) {
        remote_interface->wait_for_non_mmio_flush();
    }
}

void TTDevice::init_tt_device(std::chrono::milliseconds timeout_ms) {
    get_device_firmware()->init_firmware(timeout_ms, get_selected_noc_id());
    construct_soc_descriptor(model_->get_shared_soc_arch_descriptor());
}

uint64_t TTDevice::get_refclk_counter() { return get_device_firmware()->get_refclk_counter(get_selected_noc_id()); }

uint32_t TTDevice::get_risc_reset_state(CoreCoord core) {
    const NocId noc_id = get_selected_noc_id();
    const std::optional<RiscType> riscs_in_reset =
        require_risc_reset(get_risc_reset())->get_risc_reset_state(resolve_coordinate(core, noc_id), noc_id);
    if (!riscs_in_reset.has_value()) {
        UMD_THROW(error::RuntimeError, fmt::format("The reset state of core {} cannot be read.", core.str()));
    }
    return get_architecture_implementation()->get_soft_reset_reg_value(*riscs_in_reset);
}

void TTDevice::set_risc_reset_state(CoreCoord core, const uint32_t risc_flags) {
    write_to_device_reg(
        &risc_flags,
        core,
        get_architecture_implementation()->get_tensix_soft_reset_addr(),
        sizeof(risc_flags),
        get_selected_noc_id());
}

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc_id) {
    require_risc_reset(get_risc_reset())->assert_risc_reset(resolve_coordinate(core, noc_id), selected_riscs, noc_id);
}

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    require_risc_reset(get_risc_reset())
        ->deassert_risc_reset(resolve_coordinate(core, noc_id), selected_riscs, staggered_start, noc_id);
}

std::unique_ptr<TlbWindow> TTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    // Only the PCIe transport has TLB windows to hand out.
    auto *pcie_protocol = dynamic_cast<PcieProtocol *>(get_device_protocol());
    if (pcie_protocol == nullptr) {
        UMD_THROW(error::RuntimeError, "TLB windows need a PCIe device.");
    }
    return pcie_protocol->allocate_window(config, mapping, size);
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    return create_io_window(target, host, IoOrdering::Strict);
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host, IoOrdering ordering) {
    // Fixed now so the window keeps routing over the NOC selected when it was made, whichever thread uses it later.
    if (!target.noc.has_value()) {
        target.noc = get_selected_noc_id();
    }

    std::unique_ptr<IoWindow> model_window = model_->create_io_window(target, host);
    if (model_window != nullptr) {
        model_window->configure(target, ordering);
        return model_window;
    }

    std::unique_ptr<TlbWindow> tlb_window = get_io_window(tlb_data{}, to_tlb_mapping(host.mapping), host.size);
    tlb_window->configure(target, ordering);
    return tlb_window;
}

int TTDevice::export_dmabuf(CoreCoord core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    PcieInterface *pcie_interface = get_pcie_interface();
    if (pcie_interface == nullptr) {
        UMD_THROW(error::RuntimeError, "dma-buf export needs a PCIe device.");
    }
    return pcie_interface->export_dmabuf(resolve_coordinate(core, noc_id), addr, size, ordering, noc_id);
}

EthTrainingStatus TTDevice::read_eth_core_training_status(CoreCoord eth_core) {
    const NocId noc_id = get_selected_noc_id();
    return get_device_firmware()->get_eth_core_training_status(resolve_coordinate(eth_core, noc_id), noc_id);
}

const SocDescriptor &TTDevice::get_soc_descriptor() const {
    throw_if_uninitialized(*this, soc_descriptor_.has_value());
    return *soc_descriptor_;
}

void TTDevice::multicast_write_via_unicast(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    for (size_t x = core_start.x; x <= core_end.x; x++) {
        for (size_t y = core_start.y; y <= core_end.y; y++) {
            write_to_device(src, CoreCoord(x, y, core_start.core_type, core_start.coord_system), addr, size, noc_id);
        }
    }
}

void TTDevice::construct_soc_descriptor(const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    soc_descriptor_.emplace(soc_arch_descriptor, get_device_firmware()->get_chip_info(get_selected_noc_id()));
}

void TTDevice::set_soc_descriptor(const SocDescriptor &soc_descriptor) { soc_descriptor_ = soc_descriptor; }

void TTDevice::wire_hang_detector() {
    PcieInterface *pcie_interface = get_pcie_interface();
    if (pcie_interface == nullptr || model_->get_hang_detector() == nullptr) {
        return;
    }

    // The timeout check runs while the timed-out op still holds the protocol's window, so the detector must probe
    // through a window of its own. Without one, overruns stay unchecked rather than deadlock on that window.
    if (auto *hang_detector = dynamic_cast<HangDetectorImplementation *>(model_->get_hang_detector())) {
        try {
            hang_detector->set_noc_probe_window(get_io_window(tlb_data{}, TlbMapping::UC));
        } catch (const std::exception &e) {
            log_debug(LogUMD, "No TLB window for NOC hang probes, IO timeouts will not check for hangs: {}", e.what());
            return;
        }
    }
    pcie_interface->set_io_timeout_callback(make_io_timeout_hang_check());
}

std::function<bool(NocId)> TTDevice::make_io_timeout_hang_check() {
    return [this](NocId noc_id) { return is_noc_hung(noc_id, HangAction::RETURN); };
}

xy_pair TTDevice::resolve_coordinate(CoreCoord core, NocId noc_id) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return core;
    }
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UnresolvableCoordinateError, *this, core, noc_id);
    }
    return soc_descriptor_->translate_chip_coord_to_translated(core, noc_id);
}

}  // namespace tt::umd
