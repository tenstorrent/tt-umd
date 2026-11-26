// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device_model/simulation_tt_device_model.hpp"

#include <filesystem>
#include <tt-logger/tt-logger.hpp>

#include "soc_arch_descriptor_resolver.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/hang_detection/hang_detector.hpp"
#include "tt-umd/tt_device/reset/risc_reset.hpp"
#include "tt_device/firmware/blackhole_device_firmware.hpp"
#include "tt_device/firmware/simulation_device_firmware.hpp"
#include "tt_device/firmware/wormhole_device_firmware.hpp"
#include "tt_device/protocol/tt_sim_protocol.hpp"

namespace tt::umd {

namespace {

// A simulated device's SoC descriptor comes from the simulator's own YAML, which need not agree with
// the architecture's constants, so prefer that YAML when none is supplied -- but only while the file
// exists. A socket client rebuilds its descriptor through a temp YAML that is deleted at once, so on
// that path the recorded path is stale and the constants are the right fallback.
std::shared_ptr<SocArchDescriptor> resolve(
    const SocDescriptor &soc_descriptor, const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    if (soc_arch_descriptor != nullptr) {
        log_debug(tt::LogEmulationDriver, "Simulated device uses the SocArchDescriptor supplied by the caller.");
        return resolve_soc_arch_descriptor(soc_descriptor.arch, soc_arch_descriptor);
    }
    if (!soc_descriptor.device_descriptor_file_path.empty() &&
        std::filesystem::exists(soc_descriptor.device_descriptor_file_path)) {
        log_debug(
            tt::LogEmulationDriver,
            "Simulated device uses the SocArchDescriptor from the simulator's own {}.",
            soc_descriptor.device_descriptor_file_path);
        return std::make_shared<SocArchDescriptor>(soc_descriptor.device_descriptor_file_path);
    }
    log_debug(
        tt::LogEmulationDriver,
        "Simulated device uses the {} architecture constants: no simulator descriptor at '{}'.",
        arch_to_str(soc_descriptor.arch),
        soc_descriptor.device_descriptor_file_path);
    return resolve_soc_arch_descriptor(soc_descriptor.arch, soc_arch_descriptor);
}

// A simulator cannot hang the way a bus can: an access either reaches it in-process or throws. So
// reporting healthy is accurate rather than a stub, and a detector must exist because a device
// serving a PcieInterface is asked for one during startup.
class SimulationHangDetector final : public HangDetector {
protected:
    uint32_t read_hang_check_reg_via_bus() override { return 0; }

    uint32_t read_hang_check_reg_via_noc(NocId /*noc*/) override { return 0; }

    bool is_bus_available() const override { return true; }

    bool is_noc_available() const override { return true; }
};

// What a simulated device whose simulator models no management firmware reports: every value is
// unavailable, which the interface spells std::nullopt (or an empty list). The firmware bundle version
// has no "unavailable" form, so it is the default 0.0.0.
class SimulationFirmwareInfoProvider final : public FirmwareInfoProvider {
public:
    FirmwareBundleVersion get_firmware_version(NocId /*noc_id*/) const override { return FirmwareBundleVersion{}; }

    std::optional<uint32_t> get_eth_fw_version(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<SemVer> get_eth_fw_version_semver(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<SemVer> get_gddr_fw_version(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<SemVer> get_cm_fw_version(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<SemVer> get_dm_app_fw_version(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<SemVer> get_dm_bl_fw_version(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<SemVer> get_tt_flash_version(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint64_t> get_board_id(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint8_t> get_asic_location(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<double> get_asic_temperature(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<double> get_board_temperature(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<double> get_thm_limit_shutdown(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<double> get_thm_limit_throttle(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_therm_trip_count(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_aiclk(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_clock_freq(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_axiclk(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_arcclk(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_max_clock_freq(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_min_clock_freq(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_tdp(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_tdp_limit(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_tdc(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_vcore(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_board_power_limit(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_fan_speed(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_fan_rpm(NocId /*noc_id*/) const override { return std::nullopt; }

    std::vector<std::optional<uint32_t>> get_fan_speeds(NocId /*noc_id*/) const override { return {}; }

    std::vector<std::optional<uint32_t>> get_fan_rpms(NocId /*noc_id*/) const override { return {}; }

    std::optional<std::vector<bool>> get_eth_heartbeat_status(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<std::vector<bool>> get_eth_retrain_status(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_heartbeat_status_per_core(
        NocId /*noc_id*/) const override {
        return std::nullopt;
    }

    std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_retrain_status_per_core(
        NocId /*noc_id*/) const override {
        return std::nullopt;
    }

    std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_link_status_per_core(
        NocId /*noc_id*/) const override {
        return std::nullopt;
    }

    std::vector<DramTrainingStatus> get_dram_training_status(
        uint32_t /*num_dram_channels*/, NocId /*noc_id*/) const override {
        return {};
    }

    std::optional<GddrTelemetry> get_aggregated_dram_telemetry(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<GddrModuleTelemetry> get_dram_telemetry(GddrModule /*gddr_module*/, NocId /*noc_id*/) const override {
        return std::nullopt;
    }

    std::optional<uint16_t> get_dram_speed(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<double> get_current_max_dram_temperature(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_heartbeat(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_vdd_max(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_vdd_min(NocId /*noc_id*/) const override { return std::nullopt; }

    std::vector<std::optional<uint32_t>> get_l2cpu_clocks(NocId /*noc_id*/) const override { return {}; }

    std::optional<uint32_t> get_enabled_tensix_columns(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_enabled_tensix_rows(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_enabled_eth_cores(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_enabled_gddr(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_enabled_l2cpus(NocId /*noc_id*/) const override { return std::nullopt; }

    std::vector<PcieMode> get_pcie_modes(NocId /*noc_id*/) const override { return {}; }

    std::optional<uint32_t> get_input_power(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_tdc_limit(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_gddr_west_io_power(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_gddr_east_io_power(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<AiclkArbitration> get_aiclk_arbitration(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_host_aiclk_limit(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_smbus_error_count(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<bool> is_capability_supported(FirmwareCapability /*capability*/, NocId /*noc_id*/) const override {
        return std::nullopt;
    }

    std::optional<bool> is_capability_enabled(FirmwareCapability /*capability*/, NocId /*noc_id*/) const override {
        return std::nullopt;
    }

    std::optional<uint32_t> get_kernel_nop_throttler_stop_freq(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_nop_start_count(NocId /*noc_id*/) const override { return std::nullopt; }

    std::optional<uint32_t> get_nop_on_duration(NocId /*noc_id*/) const override { return std::nullopt; }
};

}  // namespace

SimulationTTDeviceModel::SimulationTTDeviceModel(
    const SocDescriptor &soc_descriptor, const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    arch_(soc_descriptor.arch),
    soc_arch_descriptor_(resolve(soc_descriptor, soc_arch_descriptor)),
    architecture_impl_(ArchitectureImplementation::create(soc_descriptor.arch)),
    tt_sim_protocol_(std::make_unique<TTSimProtocol>(this)),
    hang_detector_(std::make_unique<SimulationHangDetector>()),
    device_firmware_(std::make_unique<SimulationDeviceFirmware>(soc_descriptor.arch)),
    simulation_firmware_info_provider_(std::make_unique<SimulationFirmwareInfoProvider>()) {}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
SimulationTTDeviceModel::~SimulationTTDeviceModel() = default;

std::unique_ptr<IoWindow> SimulationTTDeviceModel::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host) {
    return nullptr;
}

// Serving a protocol is what lets the architecture's firmware, telemetry reader and firmware-info
// provider read a simulated device without knowing it is simulated. It is usable only once the
// device has attached itself to it, which it does when its backend comes up.
DeviceProtocol *SimulationTTDeviceModel::get_device_protocol() { return tt_sim_protocol_.get(); }

HangDetector *SimulationTTDeviceModel::get_hang_detector() { return hang_detector_.get(); }

void SimulationTTDeviceModel::use_arch_device_firmware() {
    // What every architecture firmware needs doing once built: keep it, and serve the telemetry
    // reader and info provider it owns. Taking it already constructed leaves the constructor
    // arguments at the call site, which is the only part that differs between architectures.
    auto install = [this](auto firmware) {
        auto *raw = firmware.get();
        telemetry_reader_lookup_ = [raw]() { return raw->get_firmware_telemetry_reader(); };
        info_provider_lookup_ = [raw]() { return raw->get_firmware_info_provider(); };
        device_firmware_ = std::move(firmware);
    };

    switch (arch_) {
        case tt::ARCH::WORMHOLE_B0:
            install(std::make_unique<WormholeDeviceFirmware>(
                tt_sim_protocol_.get(),
                tt_sim_protocol_.get(),
                /*jtag_interface=*/nullptr,
                /*remote_interface=*/nullptr,
                architecture_impl_.get(),
                /*kmd_lock_available=*/false));
            break;
        case tt::ARCH::BLACKHOLE:
            install(std::make_unique<BlackholeDeviceFirmware>(
                tt_sim_protocol_.get(),
                tt_sim_protocol_.get(),
                /*jtag_interface=*/nullptr,
                architecture_impl_.get(),
                /*kmd_lock_available=*/false));
            break;
        default:
            // Quasar models no ARC, no ethernet and no host BAR path, so there is nothing to read
            // and the firmware that reports nothing stays.
            break;
    }
}

DeviceFirmware *SimulationTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

RiscReset *SimulationTTDeviceModel::get_risc_reset() { return risc_reset_.get(); }

void SimulationTTDeviceModel::set_risc_reset(std::unique_ptr<RiscReset> risc_reset) {
    risc_reset_ = std::move(risc_reset);
}

FirmwareTelemetryReader *SimulationTTDeviceModel::get_firmware_telemetry_reader() {
    return telemetry_reader_lookup_ ? telemetry_reader_lookup_() : nullptr;
}

// An architecture firmware's provider exists only once that firmware is initialized, so until then the
// device reports itself uninitialized, as silicon does.
FirmwareInfoProvider *SimulationTTDeviceModel::get_firmware_info_provider() {
    return info_provider_lookup_ ? info_provider_lookup_() : simulation_firmware_info_provider_.get();
}

ArchitectureImplementation *SimulationTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

// A simulation backend supplies a full SocDescriptor of its own rather than an architecture
// descriptor for TTDevice to build one from.
SocArchDescriptor *SimulationTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

std::shared_ptr<SocArchDescriptor> SimulationTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

}  // namespace tt::umd
