// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "common/unsupported.hpp"
#include "common/utils.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/tt_device/remote_communication.hpp"
#include "tt-umd/tt_device/reset/risc_reset.hpp"
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
            UMD_NOT_SUPPORTED("A JTAG TTDevice");
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Cannot create a TTDevice for device type {}.", DeviceTypeToString.at(device_type)));
    }

    auto pci_device = std::make_unique<PCIDevice>(device_number);
    const tt::ARCH arch = pci_device->get_arch();
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_NOT_SUPPORTED("A " + arch_to_str(arch) + " TTDevice");
    }

    if (use_safe_api) {
        set_sigbus_safe_handler(true);
    }
    auto model = std::make_unique<BlackholeTTDeviceModel>(
        std::move(pci_device), use_safe_api ? IoSafety::Enabled : IoSafety::Disabled, soc_arch_descriptor);
    return std::unique_ptr<TTDevice>(new TTDevice(std::move(model)));
}

std::unique_ptr<TTDevice> TTDevice::create(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    UMD_NOT_SUPPORTED("A remote TTDevice");
}

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) { wire_hang_detector(); }

ArchitectureImplementation *TTDevice::get_architecture_implementation() { return model_->get_architecture_impl(); }

PCIDevice *TTDevice::get_pci_device() { return model_->get_pci_device(); }

// Remote devices are not supported by this build, so no device reaches its chip over another one.
RemoteCommunication *TTDevice::get_remote_communication() { return nullptr; }

DeviceProtocol *TTDevice::get_device_protocol() { return model_->get_device_protocol(); }

PcieInterface *TTDevice::get_pcie_interface() { return model_->get_pcie_interface(); }

JtagInterface *TTDevice::get_jtag_interface() { return model_->get_jtag_interface(); }

RemoteInterface *TTDevice::get_remote_interface() { return model_->get_remote_interface(); }

tt::ARCH TTDevice::get_arch() const { return model_->get_architecture_impl()->get_architecture(); }

bool TTDevice::is_pcie_hung(uint32_t data_read, HangAction action) {
    HangDetector *hang_detector = model_->get_hang_detector();
    if (hang_detector == nullptr || !hang_detector->is_bus_hung(data_read).value_or(false)) {
        return false;
    }
    if (action == HangAction::THROW) {
        UMD_THROW(error::PcieHangError, *this, data_read);
    }
    return true;
}

bool TTDevice::is_noc_hung(NocId noc, HangAction action) {
    HangDetector *hang_detector = model_->get_hang_detector();
    if (hang_detector == nullptr || !hang_detector->is_noc_hung(noc).value_or(false)) {
        return false;
    }
    if (action == HangAction::THROW) {
        UMD_THROW(error::NocHangError, *this, noc);
    }
    return true;
}

void TTDevice::read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    const auto [target_core, target_addr] = resolve_target(core, addr, size, noc_id);
    get_device_protocol()->read_data(mem_ptr, target_core, target_addr, size, noc_id);
}

void TTDevice::write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    const auto [target_core, target_addr] = resolve_target(core, addr, size, noc_id);
    get_device_protocol()->write_data(mem_ptr, target_core, target_addr, size, noc_id);
}

void TTDevice::read_from_device_reg(
    void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    const auto [target_core, target_addr] = resolve_target(core, addr, size, noc_id);
    get_device_protocol()->read_ctrl(mem_ptr, target_core, target_addr, size, noc_id, width);
}

void TTDevice::write_to_device_reg(
    const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    const auto [target_core, target_addr] = resolve_target(core, addr, size, noc_id);
    get_device_protocol()->write_ctrl(mem_ptr, target_core, target_addr, size, noc_id, width);
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
    const xy_pair start = resolve_coordinate(core_start, noc_id);
    const xy_pair end = resolve_coordinate(core_end, noc_id);
    if (!get_device_protocol()->write_to_core_range(src, start, end, addr, size, noc_id)) {
        multicast_write_via_unicast(src, size, core_start, core_end, addr, noc_id);
    }
}

void TTDevice::noc_multicast_write(const void *src, size_t size, uint64_t addr, NocId noc_id) {
    // A rectangle spanning every Tensix column would also take in the columns between them that hold other cores
    // (DRAM, ARC), so each run of adjacent Tensix columns gets a multicast of its own.
    const std::vector<CoreCoord> tensix_cores =
        get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    std::set<size_t> columns;
    std::set<size_t> rows;
    for (const CoreCoord &core : tensix_cores) {
        columns.insert(core.x);
        rows.insert(core.y);
    }
    const auto multicast_columns = [&](size_t first_column, size_t last_column) {
        noc_multicast_write(
            src,
            size,
            CoreCoord(first_column, *rows.begin(), CoreType::TENSIX, CoordSystem::TRANSLATED),
            CoreCoord(last_column, *rows.rbegin(), CoreType::TENSIX, CoordSystem::TRANSLATED),
            addr,
            noc_id);
    };
    size_t run_start = *columns.begin();
    size_t previous_column = run_start;
    for (const size_t column : columns) {
        if (column > previous_column + 1) {
            multicast_columns(run_start, previous_column);
            run_start = column;
        }
        previous_column = column;
    }
    multicast_columns(run_start, previous_column);
}

ChipInfo TTDevice::get_chip_info() { return get_device_firmware()->get_chip_info(get_selected_noc_id()); }

FirmwareBundleVersion TTDevice::get_firmware_version() {
    return get_firmware_info_provider()->get_firmware_version(get_selected_noc_id());
}

DeviceFirmware *TTDevice::get_device_firmware() const { return model_->get_device_firmware(); }

std::chrono::milliseconds TTDevice::wait_eth_core_training(
    CoreCoord eth_core, const std::chrono::milliseconds timeout_ms) {
    const NocId noc_id = get_selected_noc_id();
    const auto start = std::chrono::steady_clock::now();
    if (!get_device_firmware()->wait_eth_core_training(resolve_coordinate(eth_core, noc_id), timeout_ms, noc_id)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("ETH core {} did not finish training within {} ms.", eth_core.str(), timeout_ms.count()));
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    if (!get_device_firmware()->wait_dram_channel_training(dram_channel, timeout_ms, get_selected_noc_id())) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("DRAM channel {} did not finish training within {} ms.", dram_channel, timeout_ms.count()));
    }
}

namespace {

PcieInterface &require_pcie(PcieInterface *pcie_interface) {
    if (pcie_interface == nullptr) {
        UMD_THROW(error::RuntimeError, "BAR access needs a device on the local PCIe bus.");
    }
    return *pcie_interface;
}

}  // namespace

void TTDevice::bar_write32(uint32_t addr, uint32_t data) { require_pcie(get_pcie_interface()).bar_write32(addr, data); }

uint32_t TTDevice::bar_read32(uint32_t addr) { return require_pcie(get_pcie_interface()).bar_read32(addr); }

FirmwareTelemetryReader *TTDevice::get_firmware_telemetry_reader() const {
    FirmwareTelemetryReader *telemetry_reader = model_->get_firmware_telemetry_reader();
    if (telemetry_reader == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return telemetry_reader;
}

tt_xy_pair TTDevice::get_arc_core() const { return get_arc_core(get_selected_noc_id()); }

tt_xy_pair TTDevice::get_arc_core(const NocId noc_id) const {
    return get_device_firmware()->get_firmware_noc_coord(noc_id);
}

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const {
    FirmwareInfoProvider *firmware_info_provider = model_->get_firmware_info_provider();
    if (firmware_info_provider == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return firmware_info_provider;
}

void TTDevice::set_power_state(PowerState state, NocId noc_id) {
    get_device_firmware()->set_power_state(
        state == PowerState::BUSY ? tt::umd::PowerState::HIGH : tt::umd::PowerState::LOW, noc_id);
}

void TTDevice::set_clock_state(ClockState state, NocId noc_id) {
    get_device_firmware()->set_clock_state(state, noc_id);
}

namespace {

// Unwraps a telemetry value every initialized device of a supported architecture publishes.
template <typename T>
T require_telemetry(std::optional<T> value, const char *what) {
    if (!value.has_value()) {
        UMD_THROW(error::RuntimeError, fmt::format("The device firmware does not publish its {}.", what));
    }
    return *value;
}

}  // namespace

uint32_t TTDevice::get_clock() {
    return require_telemetry(get_firmware_info_provider()->get_aiclk(get_selected_noc_id()), "AICLK");
}

uint32_t TTDevice::get_max_clock_freq() {
    return require_telemetry(get_firmware_info_provider()->get_max_clock_freq(get_selected_noc_id()), "maximum AICLK");
}

uint32_t TTDevice::get_min_clock_freq() { return get_architecture_implementation()->get_min_clock_freq(); }

void TTDevice::advance_device_execution() {}

uint64_t TTDevice::get_board_id() {
    return require_telemetry(get_firmware_info_provider()->get_board_id(get_selected_noc_id()), "board id");
}

uint8_t TTDevice::get_asic_location() {
    return require_telemetry(get_firmware_info_provider()->get_asic_location(get_selected_noc_id()), "ASIC location");
}

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

bool TTDevice::get_noc_translation_enabled() {
    return get_device_firmware()->get_noc_translation_enabled(get_selected_noc_id());
}

double TTDevice::get_asic_temperature() {
    return require_telemetry(
        get_firmware_info_provider()->get_asic_temperature(get_selected_noc_id()), "ASIC temperature");
}

// Writes to a local device are posted straight to its NOC, so there is nothing to flush.
void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_remote() { return get_remote_interface() != nullptr; }

void TTDevice::init_tt_device(std::chrono::milliseconds timeout_ms) {
    get_device_firmware()->init_firmware(timeout_ms, get_selected_noc_id());
    construct_soc_descriptor(model_->get_shared_soc_arch_descriptor());
}

uint64_t TTDevice::get_refclk_counter() { return get_device_firmware()->get_refclk_counter(get_selected_noc_id()); }

int TTDevice::get_communication_device_id() const { return model_->get_device_protocol()->get_mmio_id(); }

IODeviceType TTDevice::get_communication_device_type() const {
    return model_->get_jtag_interface() != nullptr ? IODeviceType::JTAG : IODeviceType::PCIe;
}

uint32_t TTDevice::get_risc_reset_state(CoreCoord core) {
    uint32_t reset_state = 0;
    read_from_device_reg(
        &reset_state, core, get_architecture_implementation()->get_tensix_soft_reset_addr(), sizeof(reset_state));
    return reset_state;
}

void TTDevice::set_risc_reset_state(CoreCoord core, const uint32_t risc_flags) {
    write_to_device_reg(
        &risc_flags, core, get_architecture_implementation()->get_tensix_soft_reset_addr(), sizeof(risc_flags));
}

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc_id) {
    get_risc_reset()->assert_risc_reset(resolve_coordinate(core, noc_id), selected_riscs, noc_id);
}

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    get_risc_reset()->deassert_risc_reset(resolve_coordinate(core, noc_id), selected_riscs, staggered_start, noc_id);
}

std::unique_ptr<TlbWindow> TTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    PCIDevice *pci_device = get_pci_device();
    if (pci_device == nullptr) {
        UMD_THROW(error::RuntimeError, "TLB windows need a device on the local PCIe bus.");
    }

    std::unique_ptr<TlbHandle> tlb_handle;
    if (size != 0) {
        tlb_handle = pci_device->allocate_tlb(size, mapping);
    } else {
        // Any size will do, so take the first the driver still has a window of.
        for (const TlbSizeClass &size_class : get_architecture_tlbs(get_arch()).size_classes) {
            try {
                tlb_handle = pci_device->allocate_tlb(size_class.size, mapping);
                break;
            } catch (const std::exception &e) {
                log_debug(LogUMD, "No {:#x} byte TLB window available: {}", size_class.size, e.what());
            }
        }
        if (tlb_handle == nullptr) {
            UMD_THROW(error::RuntimeError, "No TLB window of any size is available.");
        }
    }

    auto window = std::make_unique<SiliconTlbWindow>(std::move(tlb_handle), config);
    window->set_io_timeout_hang_check(make_io_timeout_hang_check());
    return window;
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    return create_io_window(target, host, IoOrdering::Strict);
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host, IoOrdering ordering) {
    if (std::unique_ptr<IoWindow> model_window = model_->create_io_window(target, host)) {
        model_window->configure(target, ordering);
        return model_window;
    }

    // The smallest window the architecture provides that covers the requested size.
    std::optional<size_t> window_size;
    for (const TlbSizeClass &size_class : get_architecture_tlbs(get_arch()).size_classes) {
        if (size_class.size >= host.size && (!window_size.has_value() || size_class.size < *window_size)) {
            window_size = size_class.size;
        }
    }
    if (!window_size.has_value()) {
        UMD_THROW(error::RuntimeError, fmt::format("No TLB window covers the requested {:#x} bytes.", host.size));
    }

    std::unique_ptr<TlbWindow> window = get_io_window(
        tlb_data{}, host.mapping == HostMemoryCaching::UC ? TlbMapping::UC : TlbMapping::WC, *window_size);
    window->configure(target, ordering);
    return window;
}

int TTDevice::export_dmabuf(CoreCoord core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    return require_pcie(get_pcie_interface())
        .export_dmabuf(resolve_coordinate(core, noc_id), addr, size, ordering, noc_id);
}

void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) {
    SiliconTlbWindow::set_sigbus_safe_handler(set_safe_handler);
}

EthTrainingStatus TTDevice::read_eth_core_training_status(CoreCoord eth_core) {
    const NocId noc_id = get_selected_noc_id();
    return get_device_firmware()->get_eth_core_training_status(resolve_coordinate(eth_core, noc_id), noc_id);
}

const SocDescriptor &TTDevice::get_soc_descriptor() const {
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
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
    soc_descriptor_.emplace(soc_arch_descriptor, get_chip_info());
}

void TTDevice::set_soc_descriptor(const SocDescriptor &soc_descriptor) { soc_descriptor_ = soc_descriptor; }

void TTDevice::wire_hang_detector() {
    PcieInterface *pcie_interface = get_pcie_interface();
    if (pcie_interface != nullptr) {
        pcie_interface->set_io_timeout_callback(make_io_timeout_hang_check());
    }
}

std::function<bool(NocId)> TTDevice::make_io_timeout_hang_check() {
    HangDetector *hang_detector = model_->get_hang_detector();
    if (hang_detector == nullptr) {
        return {};
    }
    // A detector that cannot probe the NOC cannot confirm a hang, so the overrun counts as a false alarm.
    return [hang_detector](NocId noc_id) { return hang_detector->is_noc_hung(noc_id).value_or(false); };
}

xy_pair TTDevice::resolve_coordinate(CoreCoord core, NocId noc_id) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return core.to_pair();
    }
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UnresolvableCoordinateError, *this, core, noc_id);
    }
    return soc_descriptor_->translate_chip_coord_to_translated(core, noc_id);
}

std::pair<xy_pair, uint64_t> TTDevice::resolve_target(CoreCoord core, uint64_t addr, size_t size, NocId noc_id) const {
    if (model_->get_endpoint_resolver() != nullptr) {
        UMD_NOT_SUPPORTED("Folding a target into the address");
    }
    return {resolve_coordinate(core, noc_id), addr};
}

DmaInterface *TTDevice::get_dma_interface() { return model_->get_dma_interface(); }

RiscReset *TTDevice::get_risc_reset() {
    RiscReset *risc_reset = model_->get_risc_reset();
    if (risc_reset == nullptr) {
        UMD_THROW(error::RuntimeError, "RISC reset is not available on this device.");
    }
    return risc_reset;
}

}  // namespace tt::umd
