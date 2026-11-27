// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "arch/tlb_window_size.hpp"
#include "noc_access.hpp"
#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/driver_atomics.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/tt_device/hang_detection/hang_detector.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/jtag_interface.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/tt_device/protocol/peer_dma_interface.hpp"
#include "tt-umd/tt_device/protocol/remote_interface.hpp"
#include "tt-umd/tt_device/remote_communication.hpp"
#include "tt-umd/tt_device/reset/risc_reset.hpp"
#include "tt-umd/tt_device/tt_device_error.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/common.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/semver.hpp"
#include "tt-umd/utils/timeouts.hpp"
#include "tt_device/protocol/dma_interface.hpp"
#include "tt_device/protocol/pcie_protocol.hpp"
#include "tt_device/protocol/remote_protocol.hpp"
#include "utils.hpp"

namespace tt::umd {
enum class RiscType : std::uint64_t;

/* static */ void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) {
    SiliconTlbWindow::set_sigbus_safe_handler(set_safe_handler);
}

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) {}

DeviceFirmware *TTDevice::get_device_firmware() const { return model_->get_device_firmware(); }

void TTDevice::init_tt_device(const std::chrono::milliseconds timeout_ms, NocId noc) {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (model_->get_pcie_interface() != nullptr) {
        is_pcie_hung();
    }
    // The hang detector is an optional component, so a model that provides none simply skips the check.
    // Going through is_noc_hung() rather than the detector directly picks up its warning for a
    // protocol that cannot run the check at all.
    const NocId hang_check_noc = is_selected_noc1() ? NocId::NOC1 : NocId::NOC0;
    if (model_->get_hang_detector() != nullptr && is_noc_hung(hang_check_noc, HangAction::RETURN)) {
        UMD_THROW(error::NocHangError, *this, hang_check_noc);
    }
    // Waits for the firmware and builds the components that read what it publishes; the model's
    // firmware owns them, and the accessors below lend them onward.
    get_device_firmware()->init_firmware(timeout_ms, get_selected_noc_id());
    construct_soc_descriptor(model_->get_shared_soc_arch_descriptor(), get_selected_noc_id());
}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(
    int device_number,
    IODeviceType device_type,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    return create_tt_device(device_number, device_type, use_safe_api, soc_arch_descriptor);
}

std::unique_ptr<TTDevice> TTDevice::create(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    return create_tt_device(std::move(remote_communication), soc_arch_descriptor);
}

#ifdef TT_UMD_BUILD_SIMULATION
std::unique_ptr<TTDevice> TTDevice::create_simulation_remote(
    std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor &soc_descriptor) {
    return create_simulation_remote_tt_device(std::move(remote_communication), soc_descriptor);
}
#endif  // TT_UMD_BUILD_SIMULATION

ArchitectureImplementation *TTDevice::get_architecture_implementation() { return model_->get_architecture_impl(); }

SystemMemoryAllocator *TTDevice::get_system_memory_allocator() { return model_->get_system_memory_allocator(); }

// Returns nullptr rather than throwing when the device has no remote transport, for backward compatibility.
// Temporary, until the API reports absence explicitly (tl::expected or std::optional).
RemoteCommunication *TTDevice::get_remote_communication() {
    RemoteInterface *remote_interface = model_->get_remote_interface();
    return remote_interface == nullptr ? nullptr : remote_interface->get_remote_communication();
}

void TTDevice::set_power_state(TTDevice::PowerState state, NocId noc) {
    // TTDevice::PowerState is BUSY/IDLE and the firmware's is HIGH/LOW; converting here rather than
    // at every call site keeps the ~90 existing TTDevice::PowerState uses compiling while the two
    // enums are collapsed separately.
    get_device_firmware()->set_power_state(
        state == TTDevice::PowerState::BUSY ? tt::umd::PowerState::HIGH : tt::umd::PowerState::LOW, noc);
}

void TTDevice::set_clock_state(ClockState state, NocId /*noc*/) {
    // The per-arch overrides this replaces ignored the parameter and let ArcMessenger route on the
    // thread-selected NOC; keep that until the parameter is honored end-to-end.
    get_device_firmware()->set_clock_state(state, get_selected_noc_id());
}

bool TTDevice::get_noc_translation_enabled(NocId noc) const {
    // The overrides this replaces routed their device reads per the thread-selected NOC (via the
    // TTDevice accessors); keep that.
    return get_device_firmware()->get_noc_translation_enabled(get_selected_noc_id());
}

DeviceProtocol *TTDevice::get_device_protocol() { return model_->get_device_protocol(); }

PcieInterface *TTDevice::get_pcie_interface() { return model_->get_pcie_interface(); }

DmaInterface *TTDevice::get_dma_interface() {
    DmaInterface *dma_interface = model_->get_dma_interface();
    if (!dma_interface) {
        UMD_THROW(error::RuntimeError, "DMA interface is not available for this device.");
    }
    return dma_interface;
}

JtagInterface *TTDevice::get_jtag_interface() {
    JtagInterface *jtag_interface = model_->get_jtag_interface();
    if (!jtag_interface) {
        UMD_THROW(error::RuntimeError, "JTAG interface is not available for this device.");
    }
    return jtag_interface;
}

RemoteInterface *TTDevice::get_remote_interface() {
    RemoteInterface *remote_interface = model_->get_remote_interface();
    if (!remote_interface) {
        UMD_THROW(error::RuntimeError, "Remote interface is not available for this device.");
    }
    return remote_interface;
}

tt::ARCH TTDevice::get_arch() const { return model_->get_architecture_impl()->get_architecture(); }

bool TTDevice::is_pcie_hung(std::uint32_t data_read, TTDevice::HangAction action) {
    HangDetector *hang_detector = model_->get_hang_detector();
    if (hang_detector == nullptr) {
        UMD_THROW(error::RuntimeError, "HangDetector is not available for this device.");
    }
    auto result = hang_detector->is_bus_hung(data_read);
    if (!result.has_value()) {
        log_warning(LogUMD, "Bus hang detection is not supported for this device.");
        return false;
    }
    if (result.value()) {
        if (action == TTDevice::HangAction::THROW) {
            UMD_THROW(error::PcieHangError, *this, data_read);
        }
        return true;
    }
    return false;
}

bool TTDevice::is_noc_hung(NocId noc, TTDevice::HangAction action) {
    HangDetector *hang_detector = model_->get_hang_detector();
    if (hang_detector == nullptr) {
        UMD_THROW(error::RuntimeError, "HangDetector is not available for this device.");
    }
    auto result = hang_detector->is_noc_hung(noc);
    if (!result.has_value()) {
        log_warning(LogUMD, "NOC hang detection is not supported for this device.");
        return false;
    }
    if (result.value()) {
        if (action == TTDevice::HangAction::THROW) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format("NOC{} appears hung: you should reset the board.", static_cast<int>(noc)));
        }
        return true;
    }
    return false;
}

std::unique_ptr<TlbWindow> TTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    UMD_THROW(error::RuntimeError, "I/O windows are not supported on this device.");
}

std::unique_ptr<IoWindow> TTDevice::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    return create_io_window(target, host, IoOrdering::STRICT);
}

// Non-virtual by design: the spec surface takes config structs and hands back an IoWindow, while the
// virtual get_io_window() below it stays TLB-flavored (tlb_data, unique_ptr<TlbWindow>) as the seam
// SimulationTTDevice overrides. That split is what lets the backends behind it change shape -- e.g.
// the concrete windows implementing IoWindow directly, without TlbWindow as an intermediate base --
// without touching this signature or any caller.
std::unique_ptr<IoWindow> TTDevice::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host, IoOrdering ordering) {
    // A grid is only addressable in the translated space: without it the corners name NOC coordinates,
    // which harvesting shifts, so the rectangle they bound is not the one the caller asked for.
    UMD_ASSERT(
        !target.core_end.has_value() || get_soc_descriptor().noc_translation_enabled,
        error::RuntimeError,
        "Multicast not implemented for devices without NOC translation enabled.");

    // Resolved before the model is asked, so every window sees the same target whoever builds it.
    if (!target.noc.has_value()) {
        target.noc = get_selected_noc_id();
    }

    if (std::unique_ptr<IoWindow> window = model_->create_io_window(target, host, ordering)) {
        return window;
    }

    // The simulation devices serve windows from their own TLB allocator.
    const TlbMapping mapping = host.mapping == HostMemoryCaching::WC ? TlbMapping::WC : TlbMapping::UC;
    std::unique_ptr<TlbWindow> window =
        get_io_window({}, mapping, fit_tlb_window_size(get_arch(), target.addr, host.size));
    window->configure(target, ordering);
    return window;
}

void TTDevice::read_from_device(
    void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc, IoOrdering ordering) {
    ZoneScopedC(tracy::Color::Orange);
    const auto [xy, target] = resolve_target(core, addr, size, noc);
    get_device_protocol()->read_data(mem_ptr, xy, target, size, noc, ordering);
}

void TTDevice::write_to_device(
    const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc, IoOrdering ordering) {
    ZoneScopedC(tracy::Color::Orange);
    const auto [xy, target] = resolve_target(core, addr, size, noc);
    get_device_protocol()->write_data(mem_ptr, xy, target, size, noc, ordering);
}

void TTDevice::read_from_device_reg(
    void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc, RegisterAccessWidth width) {
    ZoneScopedC(tracy::Color::Orange);
    validate_register_access_width(get_arch(), width);
    const auto [xy, target] = resolve_target(core, addr, size, noc);
    get_device_protocol()->read_ctrl(mem_ptr, xy, target, size, noc, width);
}

void TTDevice::write_to_device_reg(
    const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc, RegisterAccessWidth width) {
    ZoneScopedC(tracy::Color::Orange);
    validate_register_access_width(get_arch(), width);
    const auto [xy, target] = resolve_target(core, addr, size, noc);
    get_device_protocol()->write_ctrl(mem_ptr, xy, target, size, noc, width);
}

bool TTDevice::wait_dram_channel_training(
    const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms, NocId noc) {
    ZoneScopedC(tracy::Color::DarkGreen);
    return get_device_firmware()->wait_dram_channel_training(dram_channel, timeout_ms, get_selected_noc_id());
}

bool TTDevice::wait_eth_core_training(CoreCoord eth_core, const std::chrono::milliseconds timeout_ms, NocId noc) {
    ZoneScopedC(tracy::Color::DarkGreen);
    const NocId tls_noc = get_selected_noc_id();
    return get_device_firmware()->wait_eth_core_training(resolve_coordinate(eth_core, tls_noc), timeout_ms, tls_noc);
}

EthTrainingStatus TTDevice::read_eth_core_training_status(CoreCoord eth_core, NocId noc) {
    const NocId tls_noc = get_selected_noc_id();
    return get_device_firmware()->get_eth_core_training_status(resolve_coordinate(eth_core, tls_noc), tls_noc);
}

FirmwareTelemetryReader *TTDevice::get_firmware_telemetry_reader() const {
    FirmwareTelemetryReader *telemetry_reader = model_->get_firmware_telemetry_reader();
    if (telemetry_reader == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return telemetry_reader;
}

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const {
    FirmwareInfoProvider *info_provider = model_->get_firmware_info_provider();
    if (info_provider == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return info_provider;
}

FirmwareBundleVersion TTDevice::get_firmware_version(NocId noc) {
    return get_firmware_info_provider()->get_firmware_version(noc);
}

void TTDevice::wait_for_non_mmio_flush(NocId noc) {
    if (model_->get_remote_interface() == nullptr) {
        return;
    }
    get_remote_interface()->get_remote_communication()->wait_for_non_mmio_flush(get_selected_noc_id());
}

bool TTDevice::is_remote() const { return model_->get_remote_interface() != nullptr; }

// A simulation model reaches its device in-process, so it has no protocol and no communication
// device to be addressed within.
int TTDevice::get_communication_device_id() const {
    DeviceProtocol *device_protocol = model_->get_device_protocol();
    return device_protocol != nullptr ? device_protocol->get_mmio_id() : -1;
}

// Derived from the transport the device actually has, rather than stored: exactly one of these
// interfaces is present, and a remote device reports the transport of the local device it is
// reached through.
IODeviceType TTDevice::get_communication_device_type() const {
    if (model_->get_pcie_interface() != nullptr) {
        return IODeviceType::PCIe;
    }
    if (model_->get_jtag_interface() != nullptr) {
        return IODeviceType::JTAG;
    }
    RemoteInterface *remote_interface = model_->get_remote_interface();
    if (remote_interface != nullptr) {
        return remote_interface->get_remote_communication()->get_local_device()->get_communication_device_type();
    }
    return IODeviceType::UNDEFINED;
}

BoardType TTDevice::get_board_type() const { return get_board_type_from_board_id(get_board_id()); }

uint64_t TTDevice::get_refclk_counter(NocId noc) const {
    return get_device_firmware()->get_refclk_counter(get_selected_noc_id());
}

uint64_t TTDevice::get_board_id() const { return get_firmware_info_provider()->get_board_id().value_or(0); }

double TTDevice::get_asic_temperature(NocId noc) const {
    return get_firmware_info_provider()->get_asic_temperature().value_or(0.0);
}

uint8_t TTDevice::get_asic_location() const { return get_firmware_info_provider()->get_asic_location().value_or(0); }

ChipInfo TTDevice::get_chip_info(NocId noc) {
    // The overrides this replaces read harvesting through ArcMessenger, which routed on the
    // thread-selected NOC; keep that.
    return get_device_firmware()->get_chip_info(get_selected_noc_id());
}

uint32_t TTDevice::get_max_clock_freq(NocId noc) const {
    return get_firmware_info_provider()->get_max_clock_freq().value_or(0);
}

uint32_t TTDevice::get_clock(NocId noc) const {
    const std::optional<uint32_t> aiclk = get_firmware_info_provider()->get_clock_freq(get_selected_noc_id());
    if (!aiclk.has_value()) {
        UMD_THROW(error::RuntimeError, "AICLK telemetry not available for this device.");
    }
    return *aiclk;
}

uint32_t TTDevice::get_min_clock_freq(NocId noc) const { return model_->get_architecture_impl()->get_min_clock_freq(); }

void TTDevice::advance_device_execution() {
    if (model_->get_remote_interface() != nullptr) {
        get_remote_interface()->get_remote_communication()->get_local_device()->advance_device_execution();
    }
}

uint32_t TTDevice::get_risc_reset_state(CoreCoord core) {
    uint32_t tensix_risc_state;
    read_from_device_reg(
        &tensix_risc_state, core, get_architecture_implementation()->get_tensix_soft_reset_addr(), sizeof(uint32_t));

    return tensix_risc_state;
}

void TTDevice::set_risc_reset_state(CoreCoord core, const uint32_t risc_flags) {
    write_to_device_reg(
        &risc_flags, core, get_architecture_implementation()->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    tt_driver_atomics::sfence();
}

RiscReset *TTDevice::get_risc_reset() {
    RiscReset *risc_reset = model_->get_risc_reset();
    if (!risc_reset) {
        UMD_THROW(error::RuntimeError, "RISC reset control is not available for this device.");
    }
    return risc_reset;
}

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc) {
    get_risc_reset()->assert_risc_reset(resolve_coordinate(core, noc), selected_riscs, noc);
}

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start, NocId noc) {
    get_risc_reset()->deassert_risc_reset(resolve_coordinate(core, noc), selected_riscs, staggered_start, noc);
}

tt_xy_pair TTDevice::get_arc_core() const { return get_arc_core(is_selected_noc1() ? NocId::NOC1 : NocId::NOC0); }

tt_xy_pair TTDevice::get_arc_core(const NocId noc) const { return get_device_firmware()->get_firmware_noc_coord(noc); }

void TTDevice::noc_multicast_write(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc) {
    UMD_ASSERT(
        get_soc_descriptor().noc_translation_enabled,
        error::RuntimeError,
        "Multicast not implemented for devices without NOC translation enabled.");
    ZoneScopedC(tracy::Color::Orange);
    xy_pair translated_start = resolve_coordinate(core_start, noc);
    xy_pair translated_end = resolve_coordinate(core_end, noc);
    bool multicast_success =
        get_device_protocol()->write_to_core_range(src, translated_start, translated_end, addr, size, noc);

    log_trace(
        LogUMD,
        "Multicast on {} chip write to cores {} - {} {}",
        is_remote() ? "remote" : "local",
        translated_start.str(),
        translated_end.str(),
        multicast_success ? "succeeded" : "failed, running unicast fallback.");

    // We need to flush the writes in case of remote communication.
    if (multicast_success && is_remote()) {
        get_remote_communication()->wait_for_non_mmio_flush(get_selected_noc_id());
    }

    if (multicast_success) {
        return;
    }

    multicast_write_via_unicast(src, size, core_start, core_end, addr, noc);
    if (is_remote()) {
        get_remote_communication()->wait_for_non_mmio_flush(get_selected_noc_id());
    }
}

void TTDevice::noc_multicast_write(const void *src, size_t size, uint64_t addr, NocId noc) {
    UMD_ASSERT(
        get_soc_descriptor().noc_translation_enabled,
        error::RuntimeError,
        "Multicast not implemented for devices without NOC translation enabled.");
    auto [start, end] =
        get_soc_descriptor().get_bounding_rectangle((noc == NocId::NOC0) ? CoordSystem::NOC0 : CoordSystem::NOC1);
    noc_multicast_write(src, size, start, end, addr, noc);
}

void TTDevice::multicast_write_via_unicast(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc) {
    CoreCoord translated_start = resolve_coordinate(core_start, noc);
    CoreCoord translated_end = resolve_coordinate(core_end, noc);
    size_t x_min = std::min(translated_start.x, translated_end.x);
    size_t x_max = std::max(translated_start.x, translated_end.x);
    size_t y_min = std::min(translated_start.y, translated_end.y);
    size_t y_max = std::max(translated_start.y, translated_end.y);

    // Iterate over all non-harvested tensix cores and unicast to those falling inside the rectangle.
    // We use the TRANSLATED coord space as NOC multicast is not available without NOC translation.
    const SocDescriptor &soc_descriptor = get_soc_descriptor();
    for (const CoreCoord &core : soc_descriptor.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
        if (core.x >= x_min && core.x <= x_max && core.y >= y_min && core.y <= y_max) {
            write_to_device(src, core, addr, size, noc);
        }
    }
}

int TTDevice::export_dmabuf(CoreCoord core, uint64_t addr, size_t size, IoOrdering ordering, NocId noc) {
    PeerDmaInterface *peer_dma_interface = model_->get_peer_dma_interface();
    if (!peer_dma_interface) {
        UMD_THROW(error::RuntimeError, "Exporting a dma-buf is not supported for this device.");
    }
    return peer_dma_interface->export_window(resolve_coordinate(core, noc), addr, size, ordering, noc);
}

void TTDevice::dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA write not supported for remote device.");
    }
    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_dma_interface()->dma_write(src, dst_addr, size, resolve_coordinate(core, noc), noc);
    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular write.
    write_to_device(src, core, dst_addr, size, noc);
}

void TTDevice::dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA read from device not supported for remote device.");
    }
    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_dma_interface()->dma_read(dst, src_addr, size, resolve_coordinate(core, noc), noc);
    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular read.
    read_from_device(dst, core, src_addr, size, noc);
}

void TTDevice::dma_write_to_core_range(
    const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA write to core range not supported for remote device.");
    }
    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_dma_interface()->dma_multicast_write(
        src, dst_addr, size, resolve_coordinate(core_start, noc), resolve_coordinate(core_end, noc), noc);

    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular multicast write.
    noc_multicast_write(src, size, core_start, core_end, dst_addr, noc);
}

void TTDevice::dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy read not supported for remote device.");
    }
    bool dma_success =
        get_dma_interface()->dma_read_zero_copy(dst_iova, src_addr, size, resolve_coordinate(core, noc), noc);
    if (!dma_success) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy read failed: no DMA buffer allocated for this device.");
    }
}

void TTDevice::dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy write not supported for remote device.");
    }
    bool dma_success =
        get_dma_interface()->dma_write_zero_copy(src_iova, dst_addr, size, resolve_coordinate(core, noc), noc);
    if (!dma_success) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy write failed: no DMA buffer allocated for this device.");
    }
}

bool TTDevice::dma_write_zero_copy_start(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc) {
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy write not supported for remote device.");
    }
    return get_dma_interface()->dma_write_zero_copy_start(src_iova, dst_addr, size, resolve_coordinate(core, noc), noc);
}

bool TTDevice::dma_read_zero_copy_start(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc) {
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy read not supported for remote device.");
    }
    return get_dma_interface()->dma_read_zero_copy_start(dst_iova, src_addr, size, resolve_coordinate(core, noc), noc);
}

DmaTransferState TTDevice::dma_write_zero_copy_check() { return get_dma_interface()->dma_write_zero_copy_check(); }

DmaTransferState TTDevice::dma_read_zero_copy_check() { return get_dma_interface()->dma_read_zero_copy_check(); }

size_t TTDevice::dma_zero_copy_max_size(uint64_t device_addr) {
    return get_dma_interface()->dma_zero_copy_max_size(device_addr);
}

const SocDescriptor &TTDevice::get_soc_descriptor() const {
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return soc_descriptor_.value();
}

void TTDevice::construct_soc_descriptor(const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor, NocId noc) {
    if (soc_arch_descriptor == nullptr) {
        soc_descriptor_ = SocDescriptor(std::make_shared<SocArchDescriptor>(get_arch()), get_chip_info(noc));
    } else {
        soc_descriptor_ = SocDescriptor(soc_arch_descriptor, get_chip_info(noc));
    }
}

void TTDevice::set_soc_descriptor(const SocDescriptor &soc_descriptor) {
    if (soc_descriptor_.has_value()) {
        UMD_THROW(error::RuntimeError, "SocDescriptor cannot be re-assgined to TTDevice.");
    }
    soc_descriptor_ = soc_descriptor;
}

// Wormhole and Blackhole carry the destination out of band, so the pair is the resolved coordinate
// and the caller's address unchanged. Quasar is the opposite: its NOC decodes a flat address into a
// destination, so the coordinate is folded into the address and the coordinate on the wire becomes
// the origin.
//
// A LITERAL coordinate is never folded. It already means "device-ready by contract" to both
// resolve_coordinate() and att::resolve_core(), which is what lets a caller address a system
// physical range directly on an architecture that otherwise resolves by coordinate.
std::pair<xy_pair, uint64_t> TTDevice::resolve_target(CoreCoord core, uint64_t addr, size_t size, NocId noc) const {
    const att::EndpointResolver *resolver = model_->get_endpoint_resolver();

    // No resolver, or an address the caller states is already device-ready: nothing to fold.
    if (resolver == nullptr || core.coord_system == CoordSystem::LITERAL) {
        return {resolve_coordinate(core, noc), addr};
    }

    // What the fold produces is a system physical address, and only NOC0 routes one. The caller
    // chooses the address space with noc further down, so folding for SYSTEM_NOC would hand a
    // system physical address to the device-local aperture and land somewhere else entirely. A
    // device-local target names itself, so it arrives LITERAL and has already returned above.
    UMD_ASSERT(
        noc != NocId::SYSTEM_NOC,
        error::RuntimeError,
        fmt::format(
            "A folded address is system physical and only NOC0 carries one, so {} cannot be reached with core {}; "
            "address a device-local target with a LITERAL coordinate.",
            noc_to_str(noc),
            core.str()));

    // The fold consumes the coordinate, so what travels is the origin: a Quasar address names its
    // own target and the protocol below refuses a coordinate that claims otherwise.
    //
    // The descriptor is only consulted to translate a coordinate into NOC0, or to look up a core
    // type the caller left unspecified. A caller supplying both has already said everything the
    // resolver asks for, and reaching for the descriptor anyway would make every folded access
    // depend on init_tt_device() having run -- a precondition the I/O path should not carry, and
    // one an architecture whose firmware cannot report chip info may never satisfy.
    if (core.coord_system == CoordSystem::NOC0 && core.core_type != CoreType::UNSPECIFIED) {
        return {xy_pair(0, 0), resolver->resolve(tt_xy_pair(core.x, core.y), core.core_type, addr, size)};
    }

    // Anything else needs translating, and translating a LOGICAL coordinate without the harvesting
    // the descriptor carries would be quietly wrong rather than merely unavailable.
    return {xy_pair(0, 0), att::resolve_core(*resolver, get_soc_descriptor(), core, addr, size)};
}

xy_pair TTDevice::resolve_coordinate(CoreCoord core, NocId noc) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return xy_pair(core.x, core.y);
    }
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UnresolvableCoordinateError, *this, core, noc);
    }
    return get_soc_descriptor().translate_chip_coord_to_translated(core, noc);
}

}  // namespace tt::umd
