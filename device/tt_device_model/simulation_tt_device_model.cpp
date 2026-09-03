// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"

#include "soc_arch_descriptor_resolver.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"
#include "umd/device/tt_device/firmware/simulation_device_firmware.hpp"
#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector.hpp"
#include "umd/device/tt_device/protocol/tt_sim_protocol.hpp"

namespace tt::umd {

namespace {

// A simulated device's SoC descriptor comes from the simulator's own YAML, which need not agree
// with the architecture's constants -- so when no descriptor is supplied, rebuild from that YAML in
// preference to the constants the shared resolver would fall back to.
std::shared_ptr<SocArchDescriptor> resolve(
    const SocDescriptor &soc_descriptor, const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    if (soc_arch_descriptor == nullptr && !soc_descriptor.device_descriptor_file_path.empty()) {
        return std::make_shared<SocArchDescriptor>(soc_descriptor.device_descriptor_file_path);
    }
    return resolve_soc_arch_descriptor(soc_descriptor.arch, soc_arch_descriptor);
}

// A simulator cannot hang the way hardware does: an access either reaches it in-process or throws
// outright, and there is no bus to stop responding. Reporting healthy is therefore accurate rather
// than a stub -- but a detector has to exist, because a device serving a PcieInterface is asked for
// one during startup.
class SimulationHangDetector final : public HangDetector {
protected:
    uint32_t read_hang_check_reg_via_bar() override { return 0; }

    uint32_t read_hang_check_reg_via_noc(NocId /*noc*/) override { return 0; }

    bool is_bus_available() override { return true; }

    bool is_noc_available() override { return true; }
};

}  // namespace

SimulationTTDeviceModel::SimulationTTDeviceModel(
    const SocDescriptor &soc_descriptor, const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    arch_(soc_descriptor.arch),
    soc_arch_descriptor_(resolve(soc_descriptor, soc_arch_descriptor)),
    architecture_impl_(ArchitectureImplementation::create(soc_descriptor.arch)),
    tt_sim_protocol_(std::make_unique<TTSimProtocol>(this)),
    hang_detector_(std::make_unique<SimulationHangDetector>()),
    device_firmware_(std::make_unique<SimulationDeviceFirmware>(soc_descriptor.arch)) {}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
SimulationTTDeviceModel::~SimulationTTDeviceModel() = default;

tt::ARCH SimulationTTDeviceModel::get_arch() const { return arch_; }

// A simulation backend has no host transport to be addressed within, so it reports no communication
// device at all.
int SimulationTTDeviceModel::get_communication_device_id() const { return -1; }

// Serving a protocol is what lets the architecture's firmware, telemetry reader and firmware-info
// provider read a simulated device without knowing it is simulated. It is usable only once the
// device has attached itself to it, which it does when its backend comes up.
DeviceProtocol *SimulationTTDeviceModel::get_device_protocol() { return tt_sim_protocol_.get(); }

HangDetector *SimulationTTDeviceModel::get_hang_detector() { return hang_detector_.get(); }

void SimulationTTDeviceModel::use_arch_device_firmware() {
    switch (arch_) {
        case tt::ARCH::WORMHOLE_B0: {
            auto firmware = std::make_unique<WormholeDeviceFirmware>(
                tt_sim_protocol_.get(),
                tt_sim_protocol_.get(),
                /*jtag_interface=*/nullptr,
                /*remote_interface=*/nullptr,
                architecture_impl_.get());
            auto *raw = firmware.get();
            telemetry_reader_lookup_ = [raw]() { return raw->get_firmware_telemetry_reader(); };
            info_provider_lookup_ = [raw]() { return raw->get_firmware_info_provider(); };
            device_firmware_ = std::move(firmware);
            break;
        }
        case tt::ARCH::BLACKHOLE: {
            auto firmware = std::make_unique<BlackholeDeviceFirmware>(
                tt_sim_protocol_.get(), tt_sim_protocol_.get(), /*jtag_interface=*/nullptr, architecture_impl_.get());
            auto *raw = firmware.get();
            telemetry_reader_lookup_ = [raw]() { return raw->get_firmware_telemetry_reader(); };
            info_provider_lookup_ = [raw]() { return raw->get_firmware_info_provider(); };
            device_firmware_ = std::move(firmware);
            break;
        }
        default:
            // Quasar models no ARC, no ethernet and no host BAR path, so there is nothing to read
            // and the firmware that reports nothing stays.
            break;
    }
}

DeviceFirmware *SimulationTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

FirmwareTelemetryReader *SimulationTTDeviceModel::get_firmware_telemetry_reader() {
    return telemetry_reader_lookup_ ? telemetry_reader_lookup_() : nullptr;
}

FirmwareInfoProvider *SimulationTTDeviceModel::get_firmware_info_provider() {
    return info_provider_lookup_ ? info_provider_lookup_() : nullptr;
}

ArchitectureImplementation *SimulationTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

// A simulation backend supplies a full SocDescriptor of its own rather than an architecture
// descriptor for TTDevice to build one from.
SocArchDescriptor *SimulationTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

std::shared_ptr<SocArchDescriptor> SimulationTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

}  // namespace tt::umd
