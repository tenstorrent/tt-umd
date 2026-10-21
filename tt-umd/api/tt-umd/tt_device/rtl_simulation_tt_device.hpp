// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "tt-umd/chip_helpers/simulation_sysmem_manager.hpp"
#include "tt-umd/chip_helpers/simulation_tlb_allocator.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/simulation/rtl_sim_communicator.hpp"
#include "tt-umd/simulation/rtl_sim_session.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/ip_block_layout.hpp"
#include "tt-umd/tt_device/simulation_tt_device.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/timeouts.hpp"

namespace tt::umd {

class RiscReset;
class RtlSimCommunicator;
class SimulationNocProtocol;
class SimulationSysmemManager;
class SimulationServerSocket;
class SimulationClient;
class SocDescriptor;
class TlbWindow;

/** One of a device's access points: its id in the build's ip_layout.yaml and the socket that reaches it. */
struct RtlSimAccessPoint {
    AccessPointId id;
    std::string host_access;
};

/** A device of the build's ip_layout.yaml: its id and its access points, in the order the file lists them. */
struct IpBlockLayoutDevice {
    IpDeviceId device;
    std::vector<RtlSimAccessPoint> access_points;
};

class RtlSimulationTTDevice : public SimulationTTDevice {
public:
    RtlSimulationTTDevice(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        int num_host_mem_channels = 0);

    /**
     * Device @p layout_device of the build's ip_layout.yaml, in @p session. The device talks over one
     * of its access points, and adds only that one's socket to the session, so the session starts
     * once every device in it has been constructed. Devices talking over the same socket share it.
     */
    RtlSimulationTTDevice(
        const std::shared_ptr<RtlSimSession>& session,
        const IpBlockLayoutDevice& layout_device,
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        int num_host_mem_channels = 0);

    ~RtlSimulationTTDevice();

    static std::unique_ptr<RtlSimulationTTDevice> create(
        const std::filesystem::path& simulator_directory, int num_host_mem_channels = 0);

    // Builds a client-mode device from device identity the connector already fetched over the
    // socket (build_soc_descriptor(device_info)); discovery uses this for a client that talks to a
    // live host. The connector owns the single GET_DEVICE_INFO fetch (it needs the backend kind to
    // pick this class), so it passes the result in rather than having this re-fetch it.
    static std::unique_ptr<RtlSimulationTTDevice> create_client(
        ChipId chip_id, std::unique_ptr<SimulationClient> client, const SimulationServerDeviceInfo& device_info);

    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc_id = NocId::DEFAULT_NOC) override;
    void deassert_risc_reset(
        CoreCoord core,
        const RiscType selected_riscs,
        bool staggered_start,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    RtlSimCommunicator* get_communicator() { return communicator_.get(); }

    std::vector<AccessPointId> get_access_points() const override;

    std::optional<uint64_t> get_flat_address(
        AccessPointId access_point,
        CoreCoord core,
        WindowFlags flags,
        IoOrdering ordering,
        uint64_t offset = 0) const override;

protected:
    SimulationBackendType backend_type() const override { return SimulationBackendType::RTL; }

    std::unique_ptr<TlbWindow> create_tlb_window(
        int tlb_index, size_t size, TlbMapping mapping, tlb_data config) override;
    void tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) override;
    void tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) override;
    bool handle_special_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) override;
    bool handle_special_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) override;
    bool should_use_cached_tlb_window() override;

private:
    // Hold the tile reset of the IP variant the SoC descriptor names, when this build knows one.
    void setup_tile_reset(ChipId chip_id);

    // Install the flat-address resolvers when the environment names an ATT map: each access point's,
    // and the one this device talks over for its own IO. Host mode only: a client hands the host a
    // translated coordinate and a core-local address, and the host resolves it, so resolving here as
    // well would fold the coordinate in twice.
    void setup_noc_address_resolver();

    // System NOC (SMN) fast path (Quasar only). `core` is a TRANSLATED coordinate; returns true when
    // the access was routed over the system NOC. These back handle_special_read/write and can grow to
    // dispatch additional special cases later.
    bool smn_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size);
    bool smn_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size);

    // Client-mode constructor: this device does not own a simulator; it forwards device operations
    // to a remote host through client. Reached only via create_client(), which validates the
    // arguments before construction.
    RtlSimulationTTDevice(
        const SocDescriptor& soc_descriptor, ChipId chip_id, std::unique_ptr<SimulationClient> client);
    // Host mode over @p socket, one of an RtlSimSession's, as device @p layout_device of its layout.
    RtlSimulationTTDevice(
        const RtlSimSocket& socket,
        const IpBlockLayoutDevice& layout_device,
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        int num_host_mem_channels);
    // Host mode over @p communicator; @p socket is empty when the communicator owns its socket.
    RtlSimulationTTDevice(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        int num_host_mem_channels,
        std::optional<RtlSimSocket> socket,
        std::optional<IpBlockLayoutDevice> layout_device,
        std::unique_ptr<RtlSimCommunicator> communicator);

    // Host-mode backend bring-up (communicator init, sysmem callbacks, TLB setup). Takes the
    // host-mem channel count because the RAM callbacks need it.
    void initialize_backend(int num_host_mem_channels);

    // setup_ runs at construction, teardown_ at destruction -- the one real host-vs-client
    // difference today: host mode drives the in-process RTL backend (communicator_), client mode
    // drives the remote host (attach_client()/detach_client()). Note client_ is owned by the
    // SimulationTTDevice base (pulled up there), not declared in this class.
    std::function<void()> setup_;
    std::function<void()> teardown_;

    // Declared before communicator_ so the session outlives the communicator using its socket.
    std::optional<RtlSimSocket> session_socket_;

    std::unique_ptr<RtlSimCommunicator> communicator_;

    // This device's place in the build's ip_layout.yaml, when it has one.
    std::optional<IpBlockLayoutDevice> layout_device_;

    // Each access point's flat addresses, when the environment names an ATT map.
    std::map<AccessPointId, att::EndpointResolver> access_point_resolvers_;

    // The IP variant's tile reset, over this device's own NOC path; the testbench performs resets
    // for a variant without one.
    std::unique_ptr<SimulationNocProtocol> noc_protocol_;
    std::unique_ptr<RiscReset> tile_reset_;
};
}  // namespace tt::umd
