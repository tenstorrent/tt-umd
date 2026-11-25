// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_backend.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "simulation/rtl_sim_ip_block_layout.hpp"
#include "simulation/rtl_sim_ip_layout_devices.hpp"
#include "simulation/simulator_paths.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/simulation/tt_sim_communicator.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/simulation_device_factory.hpp"
#include "tt-umd/tt_device/simulation_tt_device.hpp"
#include "tt-umd/tt_device/tt_sim_tt_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

class AvailableSimulationBackend final : public SimulationBackend {
public:
    // A libttsim image without a SoC descriptor is told apart by the PCI device id it reports, the way KMD tells
    // silicon apart.
    tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path) const override {
        const std::string soc_descriptor_path = find_simulator_soc_descriptor_path(simulator_path);
        if (!soc_descriptor_path.empty()) {
            return SocDescriptor::get_arch_from_soc_descriptor_path(soc_descriptor_path);
        }
        UMD_ASSERT(
            simulator_path.extension() == ".so",
            error::RuntimeError,
            fmt::format(
                "{} not found: an RTL simulator declares its architecture only through its SoC descriptor.",
                get_simulator_soc_descriptor_path(simulator_path)));

        const uint16_t device_id = TTSimCommunicator::read_pci_device_id(simulator_path);
        const tt::ARCH arch = arch_from_pci_device_id(device_id);
        UMD_ASSERT(
            arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::BLACKHOLE,
            error::RuntimeError,
            fmt::format(
                "Simulator {} reports PCI device id {:#x}, which has no built-in SoC descriptor. Place its "
                "soc_descriptor.yaml at {}.",
                simulator_path.string(),
                device_id,
                get_simulator_soc_descriptor_path(simulator_path)));
        log_debug(
            LogUMD,
            "Simulator {} reports PCI device id {:#x}: {}",
            simulator_path.string(),
            device_id,
            arch_to_str(arch));
        return arch;
    }

    // TTSim ships as a shared library; anything else is an RTL simulator build directory.
    SimulationBackendType get_backend_type(const std::filesystem::path& simulator_path) const override {
        return simulator_path.extension() == ".so" ? SimulationBackendType::TTSIM : SimulationBackendType::RTL;
    }

    std::vector<std::string> get_ip_layout_soc_descriptor_paths(
        const std::filesystem::path& simulator_path) const override {
        if (!std::filesystem::exists(simulator_path / RtlSimIpBlockLayout::FILE_NAME)) {
            return {};
        }
        const RtlSimIpBlockLayout layout(simulator_path);
        std::vector<std::string> soc_descriptor_paths;
        for (const auto& [device, access_points] : layout.get_devices()) {
            soc_descriptor_paths.push_back(layout.get_soc_descriptor(device).string());
        }
        return soc_descriptor_paths;
    }

    std::map<ChipId, std::unique_ptr<TTDevice>> create_ip_layout_tt_devices(
        const std::filesystem::path& simulator_path,
        const std::map<ChipId, size_t>& layout_devices,
        int num_host_mem_channels,
        bool launch_simulator) const override {
        const RtlSimIpBlockLayout layout(simulator_path);
        std::vector<IpDeviceId> devices_in_id_order;
        for (const auto& [device, access_points] : layout.get_devices()) {
            devices_in_id_order.push_back(device);
        }
        std::map<ChipId, IpDeviceId> ip_devices;
        for (const auto& [chip_id, position] : layout_devices) {
            ip_devices.emplace(chip_id, devices_in_id_order.at(position));
        }
        return create_rtl_sim_ip_layout_tt_devices(
            simulator_path, layout, ip_devices, num_host_mem_channels, launch_simulator);
    }

    std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
        const std::filesystem::path& simulator_path, int num_host_mem_channels) const override {
        return tt::umd::create_local_simulation_tt_devices(simulator_path, num_host_mem_channels);
    }

    std::unique_ptr<TTDevice> create_simulation_tt_device(
        const std::filesystem::path& simulator_path,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        size_t num_chips,
        int num_host_mem_channels) const override {
        return tt::umd::create_simulation_tt_device(
            simulator_path, soc_descriptor, chip_id, num_chips, num_host_mem_channels);
    }

    // Only a TTSim device sizes its system memory as it is constructed; every other device leaves it to its chip.
    void grow_host_mem_channels(TTDevice& tt_device, int num_host_mem_channels) const override {
        if (auto* tt_sim_device = dynamic_cast<TTSimTTDevice*>(&tt_device)) {
            tt_sim_device->grow_host_mem_channels(static_cast<uint32_t>(num_host_mem_channels));
        }
    }

    // Only a TTSim device is opened on an endpoint its image enumerated; RTL devices and silicon have no such BDF.
    std::optional<uint32_t> get_pci_bdf(const TTDevice& tt_device) const override {
        if (const auto* tt_sim_device = dynamic_cast<const TTSimTTDevice*>(&tt_device)) {
            return tt_sim_device->get_pci_bdf();
        }
        return std::nullopt;
    }

    // Simulated devices, local or attached to a simulation server as a client, are all SimulationTTDevices.
    std::optional<SimulationBackendType> get_backend_type(const TTDevice& tt_device) const override {
        if (const auto* simulation_device = dynamic_cast<const SimulationTTDevice*>(&tt_device)) {
            return simulation_device->backend_type();
        }
        return std::nullopt;
    }
};

}  // namespace

const SimulationBackend& simulation_backend() {
    static const AvailableSimulationBackend backend;
    return backend;
}

}  // namespace tt::umd
