// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_device.hpp"

#include <fmt/format.h>

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
#include "tracy.hpp"
#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/tt_device/protocol/remote_protocol.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/robust_mutex.hpp"
#include "umd/device/utils/semver.hpp"
#include "utils.hpp"

namespace tt::umd {
enum class RiscType : std::uint64_t;

/* static */ void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) {
    SiliconTlbWindow::set_sigbus_safe_handler(set_safe_handler);
}

TTDevice::TTDevice(
    std::unique_ptr<PCIDevice> pci_device,
    std::unique_ptr<architecture_implementation> architecture_impl,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor,
    bool use_safe_api) :
    communication_device_type_(IODeviceType::PCIe),
    communication_device_id_(pci_device->get_device_num()),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    assign_soc_arch_descriptor(soc_arch_descriptor);

    auto pcie_protocol = std::make_unique<PcieProtocol>(std::move(pci_device), use_safe_api);
    pcie_capabilities_ = pcie_protocol.get();
    device_protocol_ = std::move(pcie_protocol);
    // Initialize PCIe DMA mutex through LockManager for cross-process synchronization.
    lock_manager.initialize_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);
    if (use_safe_api) {
        set_sigbus_safe_handler(true);
    }
}

TTDevice::TTDevice(
    std::unique_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    std::unique_ptr<architecture_implementation> architecture_impl,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_type_(IODeviceType::JTAG),
    communication_device_id_(jlink_id),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    assign_soc_arch_descriptor(soc_arch_descriptor);

    auto jtag_protocol = std::make_unique<JtagProtocol>(std::move(jtag_device), jlink_id);
    jtag_capabilities_ = jtag_protocol.get();
    device_protocol_ = std::move(jtag_protocol);
}

TTDevice::TTDevice(
    std::unique_ptr<RemoteCommunication> remote_communication,
    std::unique_ptr<architecture_implementation> architecture_impl,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_type_(remote_communication->get_local_device()->get_communication_device_type()),
    communication_device_id_(remote_communication->get_local_device()->get_communication_device_id()),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    assign_soc_arch_descriptor(soc_arch_descriptor);

    auto remote_protocol = std::make_unique<RemoteProtocol>(std::move(remote_communication));
    remote_capabilities_ = remote_protocol.get();
    device_protocol_ = std::move(remote_protocol);
}

void TTDevice::probe_arc() {
    uint32_t dummy;
    read_from_arc_apb(&dummy, architecture_impl_->get_arc_reset_scratch_offset(), sizeof(dummy));  // SCRATCH_0
}

void TTDevice::assign_soc_arch_descriptor(const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    if (soc_arch_descriptor == nullptr) {
        soc_arch_descriptor_ = std::make_shared<SocArchDescriptor>(architecture_impl_->get_architecture());
        return;
    }
    UMD_ASSERT(
        soc_arch_descriptor->get_arch() == arch,
        error::RuntimeError,
        fmt::format(
            "SocArchDescriptor architecture {} does not match device architecture {}.",
            arch_to_str(soc_arch_descriptor->get_arch()),
            arch_to_str(arch)));
    soc_arch_descriptor_ = soc_arch_descriptor;
}

void TTDevice::init_tt_device(const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (pcie_capabilities_ != nullptr) {
        is_pcie_hung();
    }
    bool noc_hang_check_result =
        hang_detector_->is_noc_hung(is_selected_noc1() ? NocId::NOC1 : NocId::NOC0).value_or(false);
    if (noc_hang_check_result) {
        UMD_THROW(error::NocHangError, *this, is_selected_noc1() ? NocId::NOC1 : NocId::NOC0);
    }
    probe_arc();
    wait_arc_core_start(timeout_ms);
    arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    telemetry = ArcTelemetryReader::create_arc_telemetry_reader(this);
    firmware_info_provider = FirmwareInfoProvider::create_firmware_info_provider(this);
    construct_soc_descriptor(soc_arch_descriptor_);
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
                return std::unique_ptr<WormholeTTDevice>(
                    new WormholeTTDevice(std::move(jtag_device), device_number, soc_arch_descriptor));
            case ARCH::BLACKHOLE:
                return std::unique_ptr<BlackholeTTDevice>(
                    new BlackholeTTDevice(std::move(jtag_device), device_number, soc_arch_descriptor));
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
            return std::unique_ptr<WormholeTTDevice>(
                new WormholeTTDevice(std::move(pci_device), soc_arch_descriptor, use_safe_api));
        case ARCH::BLACKHOLE:
            return std::unique_ptr<BlackholeTTDevice>(
                new BlackholeTTDevice(std::move(pci_device), soc_arch_descriptor, use_safe_api));
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
        case tt::ARCH::WORMHOLE_B0: {
            return std::unique_ptr<WormholeTTDevice>(
                new WormholeTTDevice(std::move(remote_communication), soc_arch_descriptor));
        }
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Remote TTDevice creation is not supported for {} architecture.", arch_to_str(arch)));
    }
}

architecture_implementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

// The nullptr check for capabilities in the APIs get_pci_device, get_jtag_device and get_remote_communication
// exists for backward compatibility — these APIs are expected to return nullptr when a capability is unavailable.
// Throwing an exception would break existing behavior and require significant changes across client code.
// This approach is intended as a temporary measure until the API is updated to use tl::expected or std::optional,
// providing callers with an explicit way to check validity rather than relying on nullptr semantics.
PCIDevice *TTDevice::get_pci_device() {
    if (!pcie_capabilities_) {
        return nullptr;
    }
    return get_pcie_interface()->get_pci_device();
}

JtagDevice *TTDevice::get_jtag_device() {
    if (!jtag_capabilities_) {
        return nullptr;
    }
    return get_jtag_interface()->get_jtag_device();
}

RemoteCommunication *TTDevice::get_remote_communication() {
    if (!remote_capabilities_) {
        return nullptr;
    }
    return get_remote_interface()->get_remote_communication();
}

void TTDevice::set_power_state(bool busy) {
    if (is_remote_tt_device || !pcie_capabilities_) {
        return;
    }
    get_pci_device()->set_power_state(busy);
}

DeviceProtocol *TTDevice::get_device_protocol() { return device_protocol_.get(); }

PcieInterface *TTDevice::get_pcie_interface() {
    if (!pcie_capabilities_) {
        UMD_THROW(error::RuntimeError, "PCIe interface is not available for this device.");
    }
    return pcie_capabilities_;
}

JtagInterface *TTDevice::get_jtag_interface() {
    if (!jtag_capabilities_) {
        UMD_THROW(error::RuntimeError, "JTAG interface is not available for this device.");
    }
    return jtag_capabilities_;
}

RemoteInterface *TTDevice::get_remote_interface() {
    if (!remote_capabilities_) {
        UMD_THROW(error::RuntimeError, "Remote interface is not available for this device.");
    }
    return remote_capabilities_;
}

tt::ARCH TTDevice::get_arch() const { return arch; }

bool TTDevice::is_pcie_hung(std::uint32_t data_read, TTDevice::HangAction action) {
    if (!hang_detector_) {
        UMD_THROW(error::RuntimeError, "HangDetector is not available for this device.");
    }
    auto result = hang_detector_->is_pcie_hung(data_read);
    if (!result.has_value()) {
        log_warning(LogUMD, "PCIe hang detection is not supported for this device.");
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
    if (!hang_detector_) {
        UMD_THROW(error::RuntimeError, "HangDetector is not available for this device.");
    }
    auto result = hang_detector_->is_noc_hung(noc);
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

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc.
std::unique_ptr<TlbWindow> TTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    PCIDevice *pci = get_pci_device();
    UMD_ASSERT(
        pci != nullptr, error::RuntimeError, "TTDevice::get_io_window default implementation requires a PCIDevice.");

    if (size != 0) {
        return std::make_unique<SiliconTlbWindow>(pci->allocate_tlb(size, mapping), config);
    }

    // Caller didn't specify a size — try arch-supported sizes in preference order.
    const std::vector<size_t> &possible_sizes = get_architecture_implementation()->get_tlb_sizes();
    for (const auto &s : possible_sizes) {
        try {
            return std::make_unique<SiliconTlbWindow>(pci->allocate_tlb(s, mapping), config);
        } catch (const std::exception &e) {
            log_debug(LogUMD, "Failed to allocate TLB window of size {}: {}", s, e.what());
        }
    }

    UMD_THROW(error::RuntimeError, "Failed to allocate TLB window.");
}

void TTDevice::read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size) {
    ZoneScopedC(tracy::Color::Orange);

    device_protocol_->read_from_device(mem_ptr, resolve_coordinate(core), addr, size, get_selected_noc_id());
}

void TTDevice::write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size) {
    ZoneScopedC(tracy::Color::Orange);

    device_protocol_->write_to_device(mem_ptr, resolve_coordinate(core), addr, size, get_selected_noc_id());
}

void TTDevice::read_from_device_reg(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size) {
    ZoneScopedC(tracy::Color::Orange);

    device_protocol_->read_from_device_reg(mem_ptr, resolve_coordinate(core), addr, size, get_selected_noc_id());
}

void TTDevice::write_to_device_reg(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size) {
    ZoneScopedC(tracy::Color::Orange);

    device_protocol_->write_to_device_reg(mem_ptr, resolve_coordinate(core), addr, size, get_selected_noc_id());
}

void TTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    UMD_THROW(error::RuntimeError, "configure_iatu_region is not implemented for this device.");
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (dram_channel >= architecture_impl_->get_dram_banks_number()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Invalid DRAM channel index {}, maximum index for given architecture is {}.",
                dram_channel,
                architecture_impl_->get_dram_banks_number() - 1));
    }
    const uint32_t MAX_DRAM_RETRAIN_ATTEMPTS = get_max_dram_retrain_attempts();
    uint32_t num_retrain_dram_core = MAX_DRAM_RETRAIN_ATTEMPTS;
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status =
            get_firmware_info_provider()->get_dram_training_status(architecture_impl_->get_dram_banks_number());

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

ArcMessenger *TTDevice::get_arc_messenger() const {
    if (arc_messenger_ == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return arc_messenger_.get();
}

ArcTelemetryReader *TTDevice::get_arc_telemetry_reader() const {
    if (telemetry == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return telemetry.get();
}

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const {
    if (firmware_info_provider == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    return firmware_info_provider.get();
}

FirmwareBundleVersion TTDevice::get_firmware_version() { return get_firmware_info_provider()->get_firmware_version(); }

void TTDevice::wait_for_non_mmio_flush() {
    if (!remote_capabilities_) {
        return;
    }
    get_remote_interface()->get_remote_communication()->wait_for_non_mmio_flush();
}

bool TTDevice::is_remote() { return is_remote_tt_device; }

int TTDevice::get_communication_device_id() const { return communication_device_id_; }

IODeviceType TTDevice::get_communication_device_type() const { return communication_device_type_; }

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

uint64_t TTDevice::get_refclk_counter() {
    uint32_t high1_addr = 0;
    uint32_t high2_addr = 0;
    uint32_t low_addr = 0;
    read_from_arc_apb(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    read_from_arc_apb(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    read_from_arc_apb(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    if (high2_addr > high1_addr) {
        read_from_arc_apb(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

uint64_t TTDevice::get_board_id() { return get_firmware_info_provider()->get_board_id(); }

double TTDevice::get_asic_temperature() { return get_firmware_info_provider()->get_asic_temperature(); }

uint8_t TTDevice::get_asic_location() { return get_firmware_info_provider()->get_asic_location(); }

ChipInfo TTDevice::get_chip_info() {
    if (firmware_info_provider == nullptr) {
        UMD_THROW(error::UninitializedDeviceError, *this);
    }
    ChipInfo chip_info;

    chip_info.noc_translation_enabled = get_noc_translation_enabled();
    chip_info.board_id = get_board_id();
    chip_info.board_type = get_board_type();
    chip_info.asic_location = get_asic_location();

    return chip_info;
}

uint32_t TTDevice::get_max_clock_freq() { return get_firmware_info_provider()->get_max_clock_freq(); }

void TTDevice::advance_device_execution() {}

uint32_t TTDevice::get_risc_reset_state(tt_xy_pair core) {
    uint32_t tensix_risc_state;
    read_from_device(&tensix_risc_state, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));

    return tensix_risc_state;
}

uint32_t TTDevice::get_risc_reset_state(CoreCoord core) { return get_risc_reset_state(resolve_coordinate(core)); }

void TTDevice::set_risc_reset_state(tt_xy_pair core, const uint32_t risc_flags) {
    write_to_device(&risc_flags, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    tt_driver_atomics::sfence();
}

void TTDevice::set_risc_reset_state(CoreCoord core, const uint32_t risc_flags) {
    set_risc_reset_state(resolve_coordinate(core), risc_flags);
}

void TTDevice::assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) {
    uint32_t soft_reset_current_state = get_risc_reset_state(core);
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    uint32_t soft_reset_new = soft_reset_current_state | soft_reset_update;
    set_risc_reset_state(core, soft_reset_new);
}

void TTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    assert_risc_reset(resolve_coordinate(core), selected_riscs);
}

void TTDevice::deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start) {
    uint32_t soft_reset_current_state = get_risc_reset_state(core);
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    uint32_t soft_reset_new = soft_reset_current_state & ~soft_reset_update;
    uint32_t soft_reset_new_with_staggered_start =
        soft_reset_new | (staggered_start ? architecture_impl_->get_soft_reset_staggered_start() : 0);
    set_risc_reset_state(core, soft_reset_new_with_staggered_start);
}

void TTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    deassert_risc_reset(resolve_coordinate(core), selected_riscs, staggered_start);
}

tt_xy_pair TTDevice::get_arc_core() const { return is_selected_noc1() ? arc_core_noc1 : arc_core_noc0; }

void TTDevice::noc_multicast_write(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    ZoneScopedC(tracy::Color::Orange);
    const xy_pair core_start_xy = resolve_coordinate(core_start);
    const xy_pair core_end_xy = resolve_coordinate(core_end);
    bool multicast_success =
        device_protocol_->write_to_core_range(src, core_start_xy, core_end_xy, addr, size, get_selected_noc_id());

    log_debug(
        LogUMD,
        "Multicast on {} chip write to cores {} - {} {}",
        is_remote_tt_device ? "remote" : "local",
        core_start.str(),
        core_end.str(),
        multicast_success ? "succeeded" : "fell back to unicast");

    // We need to flush the writes in case of remote communication.
    if (multicast_success && is_remote_tt_device) {
        get_remote_communication()->wait_for_non_mmio_flush();
    }

    if (multicast_success) {
        return;
    }

    // Following is the fallback mechanism for multicast to a remote device.
    // Coordinates may be in translated or NOC0 space; we check both since their ranges don't overlap.
    // In translated space, non-harvested TENSIX rows occupy a prefix of the TENSIX translated rect.
    // In NOC0 space, we look up the core in TENSIX_CORES_NOC0 and check its row against the mask.
    const uint32_t tensix_harvesting_mask = get_chip_info().harvesting_masks.tensix_harvesting_mask;

    uint32_t num_harvested = 0;
    for (uint32_t mask = tensix_harvesting_mask; mask; mask >>= 1) {
        num_harvested += mask & 1;
    }
    const uint32_t num_active_rows = wormhole::TENSIX_GRID_SIZE.y - num_harvested;

    auto is_active_tensix = [&](uint32_t x, uint32_t y) -> bool {
        // Translated space: non-harvested TENSIX rows are the first num_active_rows rows of the rect.
        if (x >= wormhole::tensix_translated_coordinate_start_x &&
            x < wormhole::tensix_translated_coordinate_start_x + wormhole::TENSIX_GRID_SIZE.x &&
            y >= wormhole::tensix_translated_coordinate_start_y &&
            y < wormhole::tensix_translated_coordinate_start_y + num_active_rows) {
            return true;
        }
        // NOC0 space: find the core in TENSIX_CORES_NOC0 and check whether its row is harvested.
        const tt_xy_pair coord{x, y};
        auto it = std::find(wormhole::TENSIX_CORES_NOC0.begin(), wormhole::TENSIX_CORES_NOC0.end(), coord);
        if (it == wormhole::TENSIX_CORES_NOC0.end()) {
            return false;
        }
        const size_t row = (it - wormhole::TENSIX_CORES_NOC0.begin()) / wormhole::TENSIX_GRID_SIZE.x;
        return (tensix_harvesting_mask & (1u << row)) == 0;
    };

    for (uint32_t x = core_start_xy.x; x <= core_end_xy.x; ++x) {
        for (uint32_t y = core_start_xy.y; y <= core_end_xy.y; ++y) {
            const CoreCoord xy_i = CoreCoord(x, y);
            log_trace(
                LogUMD,
                "noc_multicast_write fallback unicast to core at {}, is_tensix: {}.",
                xy_i.str(),
                is_active_tensix(x, y));
            if (is_active_tensix(x, y)) {
                write_to_device(src, xy_i, addr, size);
            }
        }
    }
    get_remote_communication()->wait_for_non_mmio_flush();
}

void TTDevice::multicast_write_via_unicast(
    const void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    const xy_pair core_start_xy = resolve_coordinate(core_start);
    const xy_pair core_end_xy = resolve_coordinate(core_end);
    // No hardware multicast on simulation backends; fall back to per-core unicast.
    // TODO: investigate proper multicast support for simulations so we can remove this workaround.
    const SocDescriptor &soc_descriptor = get_soc_descriptor();
    for (uint32_t x = core_start_xy.x; x <= core_end_xy.x; ++x) {
        for (uint32_t y = core_start_xy.y; y <= core_end_xy.y; ++y) {
            // Only multicast to actual Tensix cores; skip non-Tensix cores (e.g. ARC/L2CPU, DRAM) in the range.
            const tt_xy_pair core{x, y};
            if (!soc_descriptor.is_core_of_type(core, CoreType::TENSIX, CoordSystem::TRANSLATED)) {
                continue;
            }
            write_to_device(src, core, addr, size);
        }
    }
}

void TTDevice::dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote_tt_device) {
        UMD_THROW(error::RuntimeError, "DMA write to device not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);

    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_pcie_interface()->dma_write_to_device(src, size, core, addr, get_selected_noc_id());
    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular write.
    pcie_dma_lock.unlock();
    write_to_device(src, core, addr, size);
}

void TTDevice::dma_write_to_device(const void *src, size_t size, CoreCoord core, uint64_t addr) {
    dma_write_to_device(src, size, resolve_coordinate(core), addr);
}

void TTDevice::dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote_tt_device) {
        UMD_THROW(error::RuntimeError, "DMA read from device not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);

    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success = get_pcie_interface()->dma_read_from_device(dst, size, core, addr, get_selected_noc_id());
    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular read.
    pcie_dma_lock.unlock();
    read_from_device(dst, core, addr, size);
}

void TTDevice::dma_read_from_device(void *dst, size_t size, CoreCoord core, uint64_t addr) {
    dma_read_from_device(dst, size, resolve_coordinate(core), addr);
}

void TTDevice::dma_multicast_write(void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    ZoneScopedC(tracy::Color::MediumPurple);
    if (is_remote_tt_device) {
        UMD_THROW(error::RuntimeError, "DMA multicast write not supported for remote device.");
    }
    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);

    // Returns true if DMA transfer succeeded, false if DMA is not available.
    bool dma_success =
        get_pcie_interface()->dma_multicast_write(src, size, core_start, core_end, addr, get_selected_noc_id());
    if (dma_success) {
        return;
    }

    // DMA unavailable, fall back to regular multicast write.
    pcie_dma_lock.unlock();
    noc_multicast_write(src, size, core_start, core_end, addr);
}

void TTDevice::dma_multicast_write(void *src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    dma_multicast_write(src, size, resolve_coordinate(core_start), resolve_coordinate(core_end), addr);
}

void TTDevice::dma_d2h(void *dst, uint32_t src, size_t size) { get_pcie_interface()->dma_d2h(dst, src, size); }

void TTDevice::dma_h2d(uint32_t dst, const void *src, size_t size) { get_pcie_interface()->dma_h2d(dst, src, size); }

void TTDevice::dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) {
    get_pcie_interface()->dma_d2h_zero_copy(dst, src, size);
}

void TTDevice::dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) {
    get_pcie_interface()->dma_h2d_zero_copy(dst, src, size);
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

EthTrainingStatus TTDevice::read_eth_core_training_status(CoreCoord eth_core) {
    const SocDescriptor &soc_descriptor = get_soc_descriptor();
    return read_eth_core_training_status(soc_descriptor.translate_chip_coord_to_translated(eth_core));
}

xy_pair TTDevice::resolve_coordinate(CoreCoord core) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return xy_pair(core.x, core.y);
    }
    if (!soc_descriptor_.has_value()) {
        UMD_THROW(error::UnresolvableCoordinateError, *this, core, get_selected_noc_id());
    }
    return get_soc_descriptor().translate_chip_coord_to_translated(core);
}

}  // namespace tt::umd
