// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/simulation_backend_type.hpp"

namespace tt::umd {

class ClusterDescriptor;
class SocDescriptor;
class TTDevice;

/**
 * What always-built code (topology discovery, TTDevice, the public simulator queries, SimulationConnector and the
 * simulation device factory) needs from a simulator, so that it compiles the same with and without simulation.
 *
 * In a build without simulation, every call that needs a simulator throws error::RuntimeError saying UMD was built
 * without simulation support, and calls that ask about an existing device answer as for a device no simulator
 * created.
 */
class SimulationBackend {
public:
    /**
     * How a simulated device reaches its simulator.
     */
    struct DeviceConnection {
        // Host when this process runs the simulator, Client when the device drives another process's over a socket.
        SimulationConnector::Role role = SimulationConnector::Role::Host;
        // The simulator behind the device; as a client, the one the host reports it runs.
        std::filesystem::path simulator;
        SimulationBackendType backend = SimulationBackendType::TTSIM;
        // The socket a client reaches its host through; empty for a host.
        std::filesystem::path socket;
    };

    virtual ~SimulationBackend() = default;

    /**
     * Whether simulator_path is the server directory of a simulation server, a directory holding per-chip simulation
     * sockets, rather than a simulator.
     */
    virtual bool is_server_directory(const std::filesystem::path& simulator_path) const = 0;

    /**
     * The architecture the simulator at simulator_path models, with the contract of the public get_simulator_arch in
     * tt-umd/simulation/simulator_info.hpp.
     */
    virtual tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path) const = 0;

    /**
     * Whether the simulator at simulator_path is a TTSim image or an RTL build.
     */
    virtual SimulationBackendType get_backend_type(const std::filesystem::path& simulator_path) const = 0;

    /**
     * The SoC descriptor of each device the ip_layout.yaml of the RTL simulator build at simulator_path declares, in
     * device id order; empty when the build has no ip_layout.yaml.
     */
    virtual std::vector<std::string> get_ip_layout_soc_descriptor_paths(
        const std::filesystem::path& simulator_path) const = 0;

    /**
     * Open devices of the ip_layout.yaml of the RTL simulator build at simulator_path, all served by one run of the
     * simulator, each with num_host_mem_channels host memory channels. Only the devices opened here are served, so
     * other processes may open the rest. With launch_simulator false, the run is left for another process to launch.
     *
     * @param layout_devices The devices to open, by position in device id order, keyed by the chip id each one gets.
     * @return The devices keyed by chip id.
     */
    virtual std::map<ChipId, std::unique_ptr<TTDevice>> create_ip_layout_tt_devices(
        const std::filesystem::path& simulator_path,
        const std::map<ChipId, size_t>& layout_devices,
        int num_host_mem_channels,
        bool launch_simulator) const = 0;

    /**
     * Open one device per host-visible endpoint of the simulator image at simulator_path, each with
     * num_host_mem_channels host memory channels: the simulator's counterpart of enumerating PCIe devices and opening
     * each one.
     *
     * @return The devices keyed by chip id, in endpoint order.
     */
    virtual std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
        const std::filesystem::path& simulator_path, int num_host_mem_channels) const = 0;

    /**
     * Open the device for chip_id, one of the num_chips MMIO-capable chips of a topology known without enumerating the
     * simulator at simulator_path, built with soc_descriptor and num_host_mem_channels host memory channels.
     */
    virtual std::unique_ptr<TTDevice> create_simulation_tt_device(
        const std::filesystem::path& simulator_path,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        size_t num_chips,
        int num_host_mem_channels) const = 0;

    /**
     * create_simulation_tt_device(simulator_path, num_host_mem_channels) of
     * tt-umd/tt_device/simulation_device_factory.hpp, with its contract.
     */
    virtual std::unique_ptr<TTDevice> create_simulation_tt_device(
        const std::filesystem::path& simulator_path, int num_host_mem_channels) const = 0;

    /**
     * Give tt_device at least num_host_mem_channels host memory channels when it is opened on a simulator image, for a
     * device that had to be opened before the topology saying how many it needs was known. No effect on any other
     * device, nor on one that already has that many. Only valid before the device's system memory is handed out.
     */
    virtual void grow_host_mem_channels(TTDevice& tt_device, int num_host_mem_channels) const = 0;

    /**
     * The PCI bus/device/function (bus << 8 | device << 3 | function) of the simulated endpoint tt_device drives, or
     * std::nullopt when tt_device drives no endpoint a simulator image enumerated (silicon, remote and RTL devices).
     */
    virtual std::optional<uint32_t> get_pci_bdf(const TTDevice& tt_device) const = 0;

    /**
     * Which simulator tt_device itself runs on, in this process or in the simulation server it is attached to, or
     * std::nullopt when tt_device is not a simulated device (silicon, and devices reached through another device).
     */
    virtual std::optional<SimulationBackendType> get_backend_type(const TTDevice& tt_device) const = 0;

    /**
     * How tt_device reaches its simulator, or std::nullopt when tt_device is not a simulated device (silicon, and
     * devices reached through another device).
     */
    virtual std::optional<DeviceConnection> get_device_connection(const TTDevice& tt_device) const = 0;

    /**
     * SimulationConnector::discover, with its contract, for a simulator this process hosts: options.simulator_directory
     * is a TTSim .so or an RTL simulator directory.
     */
    virtual SimulationConnector::Result discover_as_host(const SimulationConnectorOptions& options) const = 0;

    /**
     * SimulationConnector::discover, with its contract, for the server directory a simulation server serves in.
     *
     * @param sockets The live per-chip sockets in server_directory, keyed by the chip id each is named for.
     */
    virtual SimulationConnector::Result discover_as_client(
        const std::filesystem::path& server_directory,
        const std::map<ChipId, std::filesystem::path>& sockets) const = 0;

    /**
     * What answers the requests of the clients SimulationConnector::serve serves devices to, one handler per device,
     * keyed like devices: each request, as wire bytes, is answered as serve() describes, with cluster_descriptor as the
     * served topology and shutdown_handler for SHUTDOWN, and the reply comes back as wire bytes. The handlers are
     * called on the serving threads; the devices must outlive them.
     */
    virtual std::map<ChipId, std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>>
    make_server_request_handlers(
        const std::map<ChipId, TTDevice*>& devices,
        const ClusterDescriptor& cluster_descriptor,
        std::function<void()> shutdown_handler) const = 0;
};

/**
 * The simulation backend this build of UMD was built with.
 */
const SimulationBackend& simulation_backend();

}  // namespace tt::umd
