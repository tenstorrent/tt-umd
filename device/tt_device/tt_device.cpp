// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "pcie/io_window_reconfigure.hpp"
#include "tracy.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/firmware_telemetry_reader.hpp"
#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector_implementation.hpp"
#include "umd/device/tt_device/protocol/dma_interface.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/tt_device/protocol/remote_protocol.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"
#include "umd/device/tt_device_model/wormhole_tt_device_model.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/semver.hpp"
#include "utils.hpp"

namespace tt::umd {
enum class RiscType : std::uint64_t;

/* static */ void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) {
    SiliconTlbWindow::set_sigbus_safe_handler(set_safe_handler);
}

TTDevice::TTDevice(std::unique_ptr<TTDeviceModel> model) : model_(std::move(model)) {
    if (model_->get_pcie_interface() != nullptr) {
        // Initialize PCIe DMA mutex through LockManager for cross-process synchronization.
        lock_manager.initialize_mutex(
            MutexType::PCIE_DMA, get_communication_device_id(), get_communication_device_type());
    }
    wire_hang_detector();
}

DeviceFirmware *TTDevice::get_device_firmware() const { return model_->get_device_firmware(); }

void TTDevice::init_tt_device(const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (model_->get_pcie_interface() != nullptr) {
        is_pcie_hung();
    }
    // The hang detector is an optional component, so a model that provides none simply skips the check.
    HangDetector *hang_detector = model_->get_hang_detector();
    const NocId hang_check_noc = is_selected_noc1() ? NocId::NOC1 : NocId::NOC0;
    if (hang_detector != nullptr && hang_detector->is_noc_hung(hang_check_noc).value_or(false)) {
        UMD_THROW(error::NocHangError, *this, hang_check_noc);
    }
    // Waits for the firmware and builds the components that read what it publishes; the model's
    // firmware owns them, and the accessors below lend them onward.
    get_device_firmware()->init_firmware(timeout_ms, get_selected_noc_id());
    construct_soc_descriptor(model_->get_shared_soc_arch_descriptor());
}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(
    int device_number,
    IODeviceType device_type,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(
        (!use_safe_api) || (device_type == IODeviceType::PCIe),
        error::RuntimeError,
        "Safe I/O API is not supported for non-PCIe device types.");
    tt::ARCH arch = tt::ARCH::Invalid;
    if (device_type == IODeviceType::JTAG) {
        auto jtag_device = JtagDevice::create();
        arch = jtag_device->get_jtag_arch(device_number);
        switch (arch) {
            case ARCH::WORMHOLE_B0:
                return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(std::make_unique<WormholeTTDeviceModel>(
                    std::move(jtag_device), device_number, soc_arch_descriptor)));
            case ARCH::BLACKHOLE:
                return std::unique_ptr<BlackholeTTDevice>(
                    new BlackholeTTDevice(std::make_unique<BlackholeTTDeviceModel>(
                        std::move(jtag_device), device_number, soc_arch_descriptor)));
            default:
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("Creating TTDevice is not supported for {} architecture.", arch_to_str(arch)));
        }
    }

    auto pci_device = std::make_unique<PCIDevice>(device_number);
    arch = pci_device->get_arch();

    switch (arch) {
        case ARCH::WORMHOLE_B0:
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(
                std::make_unique<WormholeTTDeviceModel>(std::move(pci_device), use_safe_api, soc_arch_descriptor)));
        case ARCH::BLACKHOLE:
            return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(
                std::make_unique<BlackholeTTDeviceModel>(std::move(pci_device), use_safe_api, soc_arch_descriptor)));
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Creating TTDevice is not supported for {} architecture.", arch_to_str(arch)));
    }
}

std::unique_ptr<TTDevice> TTDevice::create(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(remote_communication != nullptr, error::RuntimeError, "RemoteCommunication pointer cannot be null.");
    tt::ARCH arch = remote_communication->get_local_device()->get_arch();
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(
                std::make_unique<WormholeTTDeviceModel>(std::move(remote_communication), soc_arch_descriptor)));
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Remote TTDevice creation is not supported for {} architecture.", arch_to_str(arch)));
    }
}

#ifdef TT_UMD_BUILD_SIMULATION
std::unique_ptr<TTDevice> TTDevice::create_simulation_remote(
    std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor &soc_descriptor) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(remote_communication != nullptr, error::RuntimeError, "RemoteCommunication pointer cannot be null.");
    tt::ARCH arch = remote_communication->get_local_device()->get_arch();
    UMD_ASSERT(
        soc_descriptor.arch == arch,
        error::RuntimeError,
        fmt::format(
            "Supplied SocDescriptor arch ({}) does not match the remote device arch ({}).",
            arch_to_str(soc_descriptor.arch),
            arch_to_str(arch)));
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            auto device =
                std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(std::make_unique<WormholeTTDeviceModel>(
                    std::move(remote_communication), /*soc_arch_descriptor=*/nullptr)));
            // This device is never run through init_tt_device() (no ARC to probe), so construct_soc_descriptor()
            // never overwrites the descriptor set here; set_soc_descriptor keeps the assign-exactly-once invariant.
            device->set_soc_descriptor(soc_descriptor);
            return device;
        }
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Remote TTDevice creation is not supported for {} architecture.", arch_to_str(arch)));
    }
}
#endif  // TT_UMD_BUILD_SIMULATION

ArchitectureImplementation *TTDevice::get_architecture_implementation() { return model_->get_architecture_impl(); }

// The nullptr check for capabilities in the APIs get_pci_device and get_remote_communication
// exists for backward compatibility — these APIs are expected to return nullptr when a capability is unavailable.
// Throwing an exception would break existing behavior and require significant changes across client code.
// This approach is intended as a temporary measure until the API is updated to use tl::expected or std::optional,
// providing callers with an explicit way to check validity rather than relying on nullptr semantics.
PCIDevice *TTDevice::get_pci_device() { return model_->get_pci_device(); }

RemoteCommunication *TTDevice::get_remote_communication() {
    RemoteInterface *remote_interface = model_->get_remote_interface();
    return remote_interface == nullptr ? nullptr : remote_interface->get_remote_communication();
}

void TTDevice::set_power_state(TTDevice::PowerState state, NocId noc_id) {
    // TTDevice::PowerState is BUSY/IDLE and the firmware's is HIGH/LOW; converting here rather than
    // at every call site keeps the ~90 existing TTDevice::PowerState uses compiling while the two
    // enums are collapsed separately.
    get_device_firmware()->set_power_state(
        state == TTDevice::PowerState::BUSY ? tt::umd::PowerState::HIGH : tt::umd::PowerState::LOW, noc_id);
}

void TTDevice::set_clock_state(ClockState state, NocId /*noc_id*/) {
    // The per-arch overrides this replaces ignored the parameter and let ArcMessenger route on the
    // thread-selected NOC; keep that until the parameter is honored end-to-end.
    get_device_firmware()->set_clock_state(state, get_selected_noc_id());
}

bool TTDevice::get_noc_translation_enabled() {
    // The overrides this replaces routed their device reads per the thread-selected NOC (via the
    // TTDevice accessors); keep that.
    return get_device_firmware()->get_noc_translation_enabled(get_selected_noc_id());
}

DeviceProtocol *TTDevice::get_device_protocol() { return model_->get_device_protocol(); }

PcieInterface *TTDevice::get_pcie_interface() {
    PcieInterface *pcie_interface = model_->get_pcie_interface();
    if (!pcie_interface) {
        UMD_THROW(error::RuntimeError, "PCIe interface is not available for this device.");
    }
    return pcie_interface;
}

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

tt::ARCH TTDevice::get_arch() const { return model_->get_arch(); }

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

void TTDevice::wire_hang_detector() {
    HangDetector *hang_detector = model_->get_hang_detector();

    // The per-op timed MMIO path is PCIe-specific, so the hang-check wiring only applies to PCIe devices.
    if (model_->get_pcie_interface() == nullptr) {
        return;
    }

    // A null detector disables hang detection: clear any previously wired callback and stop before
    // dereferencing it below.
    if (hang_detector == nullptr) {
        get_pcie_interface()->set_io_timeout_callback({});
        return;
    }

    // Route a single-op memcpy overrun to a NOC liveness check on the in-flight op's NOC: a hung NOC
    // aborts the transfer with DeviceTimeoutError; a healthy NOC lets it continue.
    get_pcie_interface()->set_io_timeout_callback(
        [this](NocId noc) -> bool { return is_noc_hung(noc, HangAction::RETURN); });

    // The liveness check runs from inside a timed-out memcpy that holds io_lock_, so it must read through a
    // dedicated, separately-locked window rather than the protocol's cached window. The window and lock live
    // in the lambda's capture; HangDetector only sees the std::function and stays unaware of either.
    //
    // get_io_window() is virtual and this runs during TTDevice's own construction, so it resolves to the
    // base implementation. That is the right one: only a PCIe device gets this far (the guard above), and
    // the sole override belongs to the simulation backends, which have no PCIe interface.
    auto window = std::shared_ptr<TlbWindow>(get_io_window({}, TlbMapping::UC));
    auto window_lock = std::make_shared<std::mutex>();
    HangDetectorImplementation *hang_detector_impl = dynamic_cast<HangDetectorImplementation *>(hang_detector);
    UMD_ASSERT(
        hang_detector_impl != nullptr,
        error::RuntimeError,
        "HangDetectorImplementation is required to wire the NOC register reader for hang detection.");
    hang_detector_impl->set_noc_reg_reader(
        [window, window_lock](tt_xy_pair core, uint64_t addr, NocId noc) -> uint32_t {
            std::lock_guard<std::mutex> lock(*window_lock);
            // The probe window has no hang check wired, so an overrun is treated as a false alarm and the read
            // completes rather than throwing; a hung NOC surfaces as HANG_READ_VALUE in `value`. A
            // DeviceTimeoutError propagating out of the probe read is therefore not expected — let it surface
            // rather than silently masking it as a hang.
            uint32_t value = 0;
            read_block_reconfigure(*window, &value, core, addr, sizeof(value), noc);
            return value;
        });
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc.
std::unique_ptr<TlbWindow> TTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    PCIDevice *pci = get_pci_device();
    UMD_ASSERT(
        pci != nullptr, error::RuntimeError, "TTDevice::get_io_window default implementation requires a PCIDevice.");

    if (size != 0) {
        return std::make_unique<SiliconTlbWindow>(pci->allocate_tlb(size, mapping), config);
    }

    // Caller didn't specify a size — try arch-supported sizes in preference order.
    for (const TlbSizeClass &size_class : get_architecture_tlbs(get_arch()).size_classes) {
        try {
            return std::make_unique<SiliconTlbWindow>(pci->allocate_tlb(size_class.size, mapping), config);
        } catch (const std::exception &e) {
            log_debug(LogUMD, "Failed to allocate TLB window of size {}: {}", size_class.size, e.what());
        }
    }

    UMD_THROW(error::RuntimeError, "Failed to allocate TLB window.");
}

void TTDevice::read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    ZoneScopedC(tracy::Color::Orange);
    get_device_protocol()->read_data(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id);
}

void TTDevice::write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    ZoneScopedC(tracy::Color::Orange);
    get_device_protocol()->write_data(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id);
}

void TTDevice::read_from_device_reg(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    ZoneScopedC(tracy::Color::Orange);
    get_device_protocol()->read_ctrl(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id);
}

void TTDevice::write_to_device_reg(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    ZoneScopedC(tracy::Color::Orange);
    get_device_protocol()->write_ctrl(mem_ptr, resolve_coordinate(core, noc_id), addr, size, noc_id);
}

void TTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    UMD_THROW(error::RuntimeError, "configure_iatu_region is not implemented for this device.");
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (dram_channel >= get_architecture_implementation()->get_dram_banks_number()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Invalid DRAM channel index {}, maximum index for given architecture is {}.",
                dram_channel,
                get_architecture_implementation()->get_dram_banks_number() - 1));
    }
    const uint32_t MAX_DRAM_RETRAIN_ATTEMPTS = get_max_dram_retrain_attempts();
    uint32_t num_retrain_dram_core = MAX_DRAM_RETRAIN_ATTEMPTS;
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status = get_firmware_info_provider()->get_dram_training_status(
            get_architecture_implementation()->get_dram_banks_number());

        if (dram_training_status.empty()) {
            log_warning(LogUMD, "DRAM training status is not available, breaking the wait for DRAM training.");
            return;
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::FAIL) {
            if (num_retrain_dram_core > 0) {
                log_warning(
                    LogUMD,
                    "DRAM training failed for channel {}, attempting retrain ({} attempts remaining).",
                    dram_channel,
                    num_retrain_dram_core - 1);
                retrain_dram_core(dram_channel);
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
            return;
        }

        utils::check_timeout(
            start,
            timeout_ms,
            fmt::format("DRAM training for channel {} timed out after {} ms", dram_channel, timeout_ms.count()));
    }
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) { return get_pcie_interface()->bar_write32(addr, data); }

uint32_t TTDevice::bar_read32(uint32_t addr) { return get_pcie_interface()->bar_read32(addr); }

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

FirmwareBundleVersion TTDevice::get_firmware_version() { return get_firmware_info_provider()->get_firmware_version(); }

void TTDevice::wait_for_non_mmio_flush() {
    if (model_->get_remote_interface() == nullptr) {
        return;
    }
    get_remote_interface()->get_remote_communication()->wait_for_non_mmio_flush();
}

bool TTDevice::is_remote() { return model_->get_remote_interface() != nullptr; }

int TTDevice::get_communication_device_id() const { return model_->get_communication_device_id(); }

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

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

uint64_t TTDevice::get_refclk_counter() {
    uint32_t high1_addr = 0;
    uint32_t high2_addr = 0;
    uint32_t low_addr = 0;
    read_from_arc_apb(
        &high1_addr, get_architecture_implementation()->get_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    read_from_arc_apb(
        &low_addr, get_architecture_implementation()->get_reset_unit_refclk_low_offset(), sizeof(low_addr));
    read_from_arc_apb(
        &high1_addr, get_architecture_implementation()->get_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    if (high2_addr > high1_addr) {
        read_from_arc_apb(
            &low_addr, get_architecture_implementation()->get_reset_unit_refclk_low_offset(), sizeof(low_addr));
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

uint64_t TTDevice::get_board_id() { return get_firmware_info_provider()->get_board_id().value_or(0); }

double TTDevice::get_asic_temperature() { return get_firmware_info_provider()->get_asic_temperature().value_or(0.0); }

uint8_t TTDevice::get_asic_location() { return get_firmware_info_provider()->get_asic_location().value_or(0); }

ChipInfo TTDevice::get_chip_info() {
    // The overrides this replaces read harvesting through ArcMessenger, which routed on the
    // thread-selected NOC; keep that.
    return get_device_firmware()->get_chip_info(get_selected_noc_id());
}

uint32_t TTDevice::get_max_clock_freq() { return get_firmware_info_provider()->get_max_clock_freq().value_or(0); }

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

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    uint32_t soft_reset_current_state = get_risc_reset_state(core);
    uint32_t soft_reset_update = get_architecture_implementation()->get_soft_reset_reg_value(selected_riscs);
    uint32_t soft_reset_new = soft_reset_current_state | soft_reset_update;
    set_risc_reset_state(core, soft_reset_new);
}

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    uint32_t soft_reset_current_state = get_risc_reset_state(core);
    uint32_t soft_reset_update = get_architecture_implementation()->get_soft_reset_reg_value(selected_riscs);
    uint32_t soft_reset_new = soft_reset_current_state & ~soft_reset_update;
    uint32_t soft_reset_new_with_staggered_start =
        soft_reset_new | (staggered_start ? get_architecture_implementation()->get_soft_reset_staggered_start() : 0);
    set_risc_reset_state(core, soft_reset_new_with_staggered_start);
}

tt_xy_pair TTDevice::get_arc_core() const { return is_selected_noc1() ? arc_core_noc1 : arc_core_noc0; }

tt_xy_pair TTDevice::get_arc_core(const NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1 : arc_core_noc0;
}

void TTDevice::noc_multicast_write(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    UMD_ASSERT(
        get_soc_descriptor().noc_translation_enabled,
        error::RuntimeError,
        "Multicast not implemented for devices without NOC translation enabled.");
    ZoneScopedC(tracy::Color::Orange);
    xy_pair translated_start = resolve_coordinate(core_start, noc_id);
    xy_pair translated_end = resolve_coordinate(core_end, noc_id);
    bool multicast_success =
        get_device_protocol()->write_to_core_range(src, translated_start, translated_end, addr, size, noc_id);

    log_trace(
        LogUMD,
        "Multicast on {} chip write to cores {} - {} {}",
        is_remote() ? "remote" : "local",
        translated_start.str(),
        translated_end.str(),
        multicast_success ? "succeeded" : "failed, running unicast fallback.");

    // We need to flush the writes in case of remote communication.
    if (multicast_success && is_remote()) {
        get_remote_communication()->wait_for_non_mmio_flush();
    }

    if (multicast_success) {
        return;
    }

    multicast_write_via_unicast(src, size, core_start, core_end, addr, noc_id);
    if (is_remote()) {
        get_remote_communication()->wait_for_non_mmio_flush();
    }
}

void TTDevice::noc_multicast_write(const void *src, size_t size, uint64_t addr, NocId noc_id) {
    UMD_ASSERT(
        get_soc_descriptor().noc_translation_enabled,
        error::RuntimeError,
        "Multicast not implemented for devices without NOC translation enabled.");
    auto [start, end] =
        get_soc_descriptor().get_bounding_rectangle((noc_id == NocId::NOC0) ? CoordSystem::NOC0 : CoordSystem::NOC1);
    noc_multicast_write(src, size, start, end, addr, noc_id);
}

void TTDevice::multicast_write_via_unicast(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    CoreCoord translated_start = resolve_coordinate(core_start, noc_id);
    CoreCoord translated_end = resolve_coordinate(core_end, noc_id);
    size_t x_min = std::min(translated_start.x, translated_end.x);
    size_t x_max = std::max(translated_start.x, translated_end.x);
    size_t y_min = std::min(translated_start.y, translated_end.y);
    size_t y_max = std::max(translated_start.y, translated_end.y);

    // Iterate over all non-harvested tensix cores and unicast to those falling inside the rectangle.
    // We use the TRANSLATED coord space as NOC multicast is not available without NOC translation.
    const SocDescriptor &soc_descriptor = get_soc_descriptor();
    for (const CoreCoord &core : soc_descriptor.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
        if (core.x >= x_min && core.x <= x_max && core.y >= y_min && core.y <= y_max) {
            write_to_device(src, core, addr, size, noc_id);
        }
    }
}

int TTDevice::export_dmabuf(CoreCoord core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "Exporting a dma-buf is not supported for remote device.");
    }
    return get_pcie_interface()->export_dmabuf(resolve_coordinate(core, noc_id), addr, size, ordering, noc_id);
}

void TTDevice::dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA write not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, get_communication_device_id(), get_communication_device_type());

    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_dma_interface()->dma_write(src, dst_addr, size, resolve_coordinate(core, noc_id), noc_id);
    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular write.
    pcie_dma_lock.unlock();
    write_to_device(src, core, dst_addr, size, noc_id);
}

void TTDevice::dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA read from device not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, get_communication_device_id(), get_communication_device_type());

    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_dma_interface()->dma_read(dst, src_addr, size, resolve_coordinate(core, noc_id), noc_id);
    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular read.
    pcie_dma_lock.unlock();
    read_from_device(dst, core, src_addr, size, noc_id);
}

void TTDevice::dma_write_to_core_range(
    const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc_id) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA write to core range not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, get_communication_device_id(), get_communication_device_type());

    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_dma_interface()->dma_multicast_write(
        src, dst_addr, size, resolve_coordinate(core_start, noc_id), resolve_coordinate(core_end, noc_id), noc_id);

    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular multicast write.
    pcie_dma_lock.unlock();
    noc_multicast_write(src, size, core_start, core_end, dst_addr, noc_id);
}

void TTDevice::dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy read not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, get_communication_device_id(), get_communication_device_type());

    bool dma_success =
        get_dma_interface()->dma_read_zero_copy(dst_iova, src_addr, size, resolve_coordinate(core, noc_id), noc_id);
    if (!dma_success) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy read failed: no DMA buffer allocated for this device.");
    }
}

void TTDevice::dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote()) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy write not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, get_communication_device_id(), get_communication_device_type());

    bool dma_success =
        get_dma_interface()->dma_write_zero_copy(src_iova, dst_addr, size, resolve_coordinate(core, noc_id), noc_id);
    if (!dma_success) {
        UMD_THROW(error::RuntimeError, "DMA zero-copy write failed: no DMA buffer allocated for this device.");
    }
}

const SocDescriptor &TTDevice::get_soc_descriptor() const {
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return soc_descriptor_.value();
}

void TTDevice::construct_soc_descriptor(const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    if (soc_arch_descriptor == nullptr) {
        soc_descriptor_ = SocDescriptor(std::make_shared<SocArchDescriptor>(get_arch()), get_chip_info());
    } else {
        soc_descriptor_ = SocDescriptor(soc_arch_descriptor, get_chip_info());
    }
}

void TTDevice::set_soc_descriptor(const SocDescriptor &soc_descriptor) {
    if (soc_descriptor_.has_value()) {
        UMD_THROW(error::RuntimeError, "SocDescriptor cannot be re-assgined to TTDevice.");
    }
    soc_descriptor_ = soc_descriptor;
}

xy_pair TTDevice::resolve_coordinate(CoreCoord core, NocId noc_id) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return xy_pair(core.x, core.y);
    }
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UnresolvableCoordinateError, *this, core, noc_id);
    }
    return get_soc_descriptor().translate_chip_coord_to_translated(core, noc_id);
}

}  // namespace tt::umd
