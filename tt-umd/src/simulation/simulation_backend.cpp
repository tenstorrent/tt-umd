// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_backend.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

#include "simulation/rtl_sim_att_program.hpp"
#include "simulation/rtl_sim_ip_block_layout.hpp"
#include "simulation/rtl_sim_session.hpp"
#include "simulation/simulation_client.hpp"
#include "simulation/simulation_device_identity.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "simulation/simulator_paths.hpp"
#include "simulation/tt_sim_communicator.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device/rtl_simulation_tt_device.hpp"
#include "tt_device/simulation_tt_device.hpp"
#include "tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

namespace {

// Create the devices of an RTL simulator build with an ip_layout.yaml: one RtlSimulationTTDevice per entry of
// ip_devices (chip id -> the layout's device), each given all its access points, in one shared RtlSimSession. Each
// device picks the access point it talks over, and the session then serves those sockets only. With launch_simulator
// false the session serves its sockets without launching run.sh, and another process launches it.
std::map<ChipId, std::unique_ptr<TTDevice>> create_rtl_sim_ip_layout_tt_devices(
    const std::filesystem::path& simulator_directory,
    const RtlSimIpBlockLayout& layout,
    const std::map<ChipId, IpDeviceId>& ip_devices,
    int num_host_mem_channels,
    bool launch_simulator) {
    // Each device adds the socket it talks over to the session, which serves them once every device
    // exists.
    auto session = std::make_shared<RtlSimSession>(simulator_directory, std::vector<std::string>{}, launch_simulator);
    const std::map<IpDeviceId, std::vector<AccessPointId>> layout_devices = layout.get_devices();
    std::map<ChipId, std::unique_ptr<RtlSimulationTTDevice>> rtl_devices;
    for (const auto& [chip_id, ip_device_id] : ip_devices) {
        RtlSimDeviceAccess layout_device{ip_device_id, {}};
        for (const AccessPointId access_point : layout_devices.at(ip_device_id)) {
            layout_device.access_points.push_back(
                {access_point, layout.get_host_access(access_point), layout.get_location(access_point).flat_address});
        }
        const SocDescriptor soc_descriptor(
            std::make_shared<SocArchDescriptor>(layout.get_soc_descriptor(ip_device_id).string()));
        rtl_devices[chip_id] = std::make_unique<RtlSimulationTTDevice>(
            session, layout_device, simulator_directory, soc_descriptor, chip_id, num_host_mem_channels);
    }
    session->start();

    // The build's ATT programming, if it ships one, goes in before any core runs.
    const std::optional<RtlSimAttProgram> att_program = RtlSimAttProgram::load(simulator_directory);

    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    for (const auto& [chip_id, ip_device_id] : ip_devices) {
        if (att_program.has_value()) {
            att_program->apply(ip_device_id, *rtl_devices.at(chip_id)->get_communicator());
        }
        rtl_devices.at(chip_id)->check_att_matches_map();
        devices[chip_id] = std::move(rtl_devices.at(chip_id));
    }
    return devices;
}

// The topology the opened devices sit in. A simulator build, or a host serving one, may ship a
// cluster descriptor; when it does not, the chips that were actually opened are the whole topology,
// so a mock over exactly those says the same thing the rest of UMD says about a descriptor-less
// simulator.
std::shared_ptr<ClusterDescriptor> build_topology(
    const std::string& yaml,
    const std::map<ChipId, std::unique_ptr<TTDevice>>& devices,
    tt::ARCH arch,
    bool noc_translation_enabled) {
    if (!yaml.empty()) {
        return ClusterDescriptor::create_from_yaml_content(yaml);
    }
    std::unordered_set<ChipId> chip_ids;
    for (const auto& [chip_id, device] : devices) {
        chip_ids.insert(chip_id);
    }
    return ClusterDescriptor::create_mock_cluster(chip_ids, arch, noc_translation_enabled);
}

// Client path: build the device class the host reports over the wire. A client runs no local
// backend, so the class mostly just names the device; the backend kind still comes from the host so
// the right class is instantiated (and so this stays correct if the classes diverge).
std::unique_ptr<TTDevice> make_client_device(
    ChipId chip_id, std::unique_ptr<SimulationClient> client, const SimulationServerDeviceInfo& info) {
    switch (info.backend_type) {
        case SimulationBackendType::TTSIM:
            return TTSimTTDevice::create_client(chip_id, std::move(client), info);
        case SimulationBackendType::RTL:
            return RtlSimulationTTDevice::create_client(chip_id, std::move(client), info);
    }
    // A value outside the enum means a corrupt/incompatible reply from the host; fail loudly rather
    // than silently defaulting to a backend class.
    UMD_THROW(
        error::RuntimeError,
        fmt::format("Host reported an unknown simulation backend kind ({})", static_cast<int>(info.backend_type)));
}

// What every socket of one Server answers alike. Shared by their request handlers, which outlive serve().
struct ServedTopology {
    std::string cluster_descriptor_yaml;
    std::function<void()> shutdown_handler;
};

// The identity a client builds its device from. A device reached through another chip runs on no simulator directly,
// so it is described without a simulator path.
SimulationServerDeviceInfo describe_served_device(const TTDevice& device) {
    const std::optional<SimulationBackend::DeviceConnection> connection =
        simulation_backend().get_device_connection(device);
    return describe_device(
        device.get_soc_descriptor(),
        device.get_simulation_backend().value_or(SimulationBackendType::TTSIM),
        connection.has_value() ? connection->simulator : std::filesystem::path{});
}

// Serves one socket request against device: decodes the wire request, runs it and encodes the reply. Runs on the
// socket's connection threads, one per attached client; device_mutex keeps their accesses to device one at a time.
std::vector<uint8_t> handle_request(
    TTDevice& device,
    std::mutex& device_mutex,
    const ServedTopology& served,
    const std::vector<uint8_t>& request_bytes) {
    const SimulationServerRequest request = decode_request(request_bytes);

    // Device info and the cluster descriptor have reply messages of their own.
    if (request.command == SimulationServerCommand::GET_DEVICE_INFO) {
        try {
            return encode(describe_served_device(device));
        } catch (const std::exception& e) {
            log_warning(LogUMD, "Simulation server failed to serve device info: {}", e.what());
            SimulationServerDeviceInfo info;
            info.status = -1;
            return encode(info);
        }
    }
    if (request.command == SimulationServerCommand::GET_CLUSTER_DESCRIPTOR) {
        SimulationServerClusterDescriptor cluster_descriptor;
        cluster_descriptor.status = 0;
        cluster_descriptor.yaml = served.cluster_descriptor_yaml;
        return encode(cluster_descriptor);
    }
    // The handler only signals the owner; the owner tears the Server down from its own thread, which joins this one
    // after the acknowledgement is sent.
    if (request.command == SimulationServerCommand::SHUTDOWN) {
        if (served.shutdown_handler) {
            served.shutdown_handler();
        }
        return encode(SimulationServerResponse{});
    }

    // The client already translated the coordinate, so it must not be translated again.
    const CoreCoord core{request.x, request.y, CoreType::UNSPECIFIED, CoordSystem::LITERAL};

    SimulationServerResponse response;
    try {
        const std::lock_guard<std::mutex> lock(device_mutex);
        switch (request.command) {
            case SimulationServerCommand::READ:
                response.data.resize(request.size);
                device.read_from_device(response.data.data(), core, request.address, request.size);
                break;
            case SimulationServerCommand::WRITE:
                UMD_ASSERT(
                    request.data.size() >= request.size,
                    error::RuntimeError,
                    fmt::format(
                        "Write request payload ({} bytes) is smaller than its size field ({})",
                        request.data.size(),
                        request.size));
                device.write_to_device(request.data.data(), core, request.address, request.size);
                break;
            default:
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("Unknown simulation server command {}", static_cast<int>(request.command)));
        }
    } catch (const std::exception& e) {
        // A failed access is the client's to handle, as a silicon access error would be; the connection stays up.
        log_warning(LogUMD, "Simulation server failed to serve a client request: {}", e.what());
        response.status = -1;
        response.data.clear();
    }
    return encode(response);
}

class AvailableSimulationBackend final : public SimulationBackend {
public:
    // Stale sockets count too, so that a server directory whose host is gone is reported as such rather than taken for
    // a simulator.
    bool is_server_directory(const std::filesystem::path& simulator_path) const override {
        std::error_code ec;
        return std::filesystem::is_directory(simulator_path, ec) &&
               !SimulationServerSocket::sockets_in_directory(simulator_path).empty();
    }

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
        const std::filesystem::path& simulator_path,
        int num_host_mem_channels,
        const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor) const override {
        UMD_ASSERT(
            simulator_path.extension() == ".so",
            error::RuntimeError,
            fmt::format(
                "Enumerating a simulator's endpoints needs a libttsim .so; {} is not one. An RTL "
                "simulator exposes no PCI config space to enumerate.",
                simulator_path.string()));

        // Ask the image which endpoints it has, before anything is brought up -- config space only
        // answers while the image runs, and starting an already-running image is fatal.
        const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path);
        UMD_ASSERT(
            !bdfs.empty(),
            error::RuntimeError,
            fmt::format("Simulator {} exposes no host-visible PCI endpoints.", simulator_path.string()));

        std::map<ChipId, std::unique_ptr<TTDevice>> devices;
        for (size_t index = 0; index < bdfs.size(); ++index) {
            // Chips are numbered densely in BDF order, as silicon numbers them in enumeration order; for
            // a linear image (chip N at bus 0, device N) that is the device number. The BDF travels with
            // the chip, so its communicator reaches its own endpoint however sparse the layout is.
            const auto chip_id = static_cast<ChipId>(index);
            devices.emplace(
                chip_id,
                TTSimTTDevice::create_for_chip(
                    simulator_path, chip_id, num_host_mem_channels, bdfs.size(), bdfs[index], soc_arch_descriptor));
        }
        log_debug(tt::LogEmulationDriver, "Simulator {} exposes {} endpoint(s)", simulator_path.string(), bdfs.size());
        return devices;
    }

    std::unique_ptr<TTDevice> create_simulation_tt_device(
        const std::filesystem::path& simulator_path,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        size_t num_chips,
        int num_host_mem_channels) const override {
        if (simulator_path.extension() == ".so") {
            return std::make_unique<TTSimTTDevice>(
                simulator_path, soc_descriptor, chip_id, num_host_mem_channels, num_chips);
        }
        log_info(tt::LogEmulationDriver, "Instantiating RTL simulation device");
        return std::make_unique<RtlSimulationTTDevice>(simulator_path, soc_descriptor, chip_id, num_host_mem_channels);
    }

    std::unique_ptr<TTDevice> create_simulation_tt_device(
        const std::filesystem::path& simulator_path, int num_host_mem_channels) const override {
        if (simulator_path.extension() == ".so") {
            return TTSimTTDevice::create(simulator_path, num_host_mem_channels);
        }
        return RtlSimulationTTDevice::create(simulator_path, num_host_mem_channels);
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

    std::optional<DeviceConnection> get_device_connection(const TTDevice& tt_device) const override {
        const auto* simulation_device = dynamic_cast<const SimulationTTDevice*>(&tt_device);
        if (simulation_device == nullptr) {
            return std::nullopt;
        }
        DeviceConnection connection;
        connection.role =
            simulation_device->client_mode() ? SimulationConnector::Role::Client : SimulationConnector::Role::Host;
        connection.simulator = simulation_device->simulator_path();
        connection.backend = simulation_device->backend_type();
        connection.socket = simulation_device->socket_path();
        return connection;
    }

    SimulationConnector::Result discover_as_host(const SimulationConnectorOptions& options) const override {
        SimulationConnector::Result result;
        const std::filesystem::path& simulator_path = options.simulator_directory;
        // Host: single chip for now. By default the host device stays private in-process (the direct hot path);
        // serving it is opt-in.
        constexpr ChipId chip_id = 0;
        result.devices.emplace(chip_id, create_simulation_tt_device(simulator_path, options.num_host_mem_channels));
        const SocDescriptor& soc_descriptor = result.devices.at(chip_id)->get_soc_descriptor();

        result.connection.role = SimulationConnector::Role::Host;
        result.connection.simulator = simulator_path;
        result.connection.backend = get_backend_type(simulator_path);
        result.connection.arch = soc_descriptor.arch;
        // The host's topology is what the simulator build describes, never one a caller hands in: the
        // simulator was not configured from it, so it could name chips that have no device here.
        if (options.cluster_descriptor != nullptr) {
            log_warning(
                LogUMD,
                "Ignoring the cluster descriptor passed to the simulation host for {}; reporting the simulator's own "
                "topology instead.",
                simulator_path.string());
        }
        result.cluster_descriptor = build_topology(
            describe_cluster(simulator_path).yaml,
            result.devices,
            result.connection.arch,
            soc_descriptor.noc_translation_enabled);
        // Serve that same topology, so every client that attaches sees the cluster this host reports.
        if (options.serve_over_sockets) {
            result.server = SimulationConnector::serve(
                {{chip_id, result.devices.at(chip_id).get()}}, *result.cluster_descriptor, options.server_directory);
            result.connection.server_directory = result.server->server_directory();
            result.connection.sockets = result.server->sockets();
        }
        return result;
    }

    SimulationConnector::Result discover_as_client(
        const std::filesystem::path& server_directory,
        const std::map<ChipId, std::filesystem::path>& sockets) const override {
        SimulationConnector::Result result;
        std::map<ChipId, std::unique_ptr<TTDevice>>& devices = result.devices;
        // Taken from the first socket that answers, alongside the identity already fetched there:
        // one host serves the whole directory, so its topology describes every chip in it.
        std::string served_topology_yaml;
        bool noc_translation_enabled = false;
        // Multi-chip: one client device per live socket in the directory. Chip ids come from the socket names, so
        // they match the host's. A failure on one socket (a dead or wedged host) only skips that chip -- it must not
        // abort attaching to the healthy ones.
        for (const auto& [chip_id, socket_file] : sockets) {
            try {
                auto client = std::make_unique<SimulationClient>(socket_file);
                // The connector owns the single GET_DEVICE_INFO fetch: it needs the backend type to
                // pick the device class, and passes the fetched identity into create_client so it is
                // not fetched again.
                const SimulationServerDeviceInfo info = fetch_device_info_from_host(*client);
                // A server directory is claimed atomically by one host (see
                // allocate_server_directory), so every socket in it is served by one process
                // running one simulator: the first socket that answers describes the whole
                // directory. Warn rather than silently describing only the first if that ever
                // fails to hold -- a hand-assembled directory mixing two servers' sockets is the
                // only way it can, and it should not pass unnoticed.
                if (result.devices.empty()) {
                    result.connection.simulator = info.simulator_path;
                    result.connection.backend = info.backend_type;
                    result.connection.arch = static_cast<tt::ARCH>(info.arch);
                    noc_translation_enabled = info.noc_translation_enabled;
                    // Fetched here, while this client is still ours to send on: it is moved into
                    // the device below.
                    served_topology_yaml = fetch_cluster_descriptor_yaml(*client);
                } else if (
                    info.simulator_path != result.connection.simulator.string() ||
                    info.backend_type != result.connection.backend ||
                    static_cast<tt::ARCH>(info.arch) != result.connection.arch) {
                    log_warning(
                        LogUMD,
                        "Simulation socket {} (chip {}) reports a different simulation than the rest of {}: "
                        "{} ({}/{}) instead of {} ({}/{}). Reporting the first; the directory is serving more "
                        "than one host.",
                        socket_file.string(),
                        chip_id,
                        server_directory.string(),
                        info.simulator_path,
                        arch_to_str(static_cast<tt::ARCH>(info.arch)),
                        info.backend_type == SimulationBackendType::TTSIM ? "ttsim" : "rtl",
                        result.connection.simulator.string(),
                        arch_to_str(result.connection.arch),
                        result.connection.backend == SimulationBackendType::TTSIM ? "ttsim" : "rtl");
                }
                devices.emplace(chip_id, make_client_device(chip_id, std::move(client), info));
                // Only once the device is in: make_client_device() throws on a backend kind this
                // build doesn't know, and the catch below skips the chip. Recording the socket
                // first would leave connection.sockets naming a chip that has no device.
                result.connection.sockets.emplace(chip_id, socket_file);
            } catch (const std::exception& e) {
                log_warning(
                    LogUMD, "Skipping simulation socket {} (chip {}): {}", socket_file.string(), chip_id, e.what());
            }
        }
        // Every socket was present but none answered -- surface it rather than returning an empty,
        // successful-looking cluster.
        UMD_ASSERT(
            !devices.empty(),
            error::RuntimeError,
            fmt::format("No reachable simulation hosts among the sockets in {}", server_directory.string()));
        result.connection.role = SimulationConnector::Role::Client;
        result.connection.server_directory = server_directory;
        result.cluster_descriptor =
            build_topology(served_topology_yaml, devices, result.connection.arch, noc_translation_enabled);
        return result;
    }

    std::map<ChipId, std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>> make_server_request_handlers(
        const std::map<ChipId, TTDevice*>& devices,
        const ClusterDescriptor& cluster_descriptor,
        std::function<void()> shutdown_handler) const override {
        const auto served = std::make_shared<const ServedTopology>(
            ServedTopology{cluster_descriptor.serialize(), std::move(shutdown_handler)});
        std::map<ChipId, std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>> request_handlers;
        for (const auto& [chip_id, device] : devices) {
            request_handlers.emplace(
                chip_id,
                [device = device, served, device_mutex = std::make_shared<std::mutex>()](
                    const std::vector<uint8_t>& request_bytes) {
                    return handle_request(*device, *device_mutex, *served, request_bytes);
                });
        }
        return request_handlers;
    }
};

}  // namespace

const SimulationBackend& simulation_backend() {
    static const AvailableSimulationBackend backend;
    return backend;
}

}  // namespace tt::umd
