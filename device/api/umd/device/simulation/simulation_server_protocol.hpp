// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tt::umd {

// The one place that defines the UMD <-> simulation host wire protocol: the messages a UMD
// client exchanges with a simulation host ("the card") over its per-chip UNIX socket, plus the
// helpers that (de)serialize them. See simulation_server_protocol.fbs for the schema.
//
// Scope today is device-memory access -- the bulk of the traffic. It grows one message at a time
// as the server work lands (session/attach, TLB windows, sysmem, run/reset, ...), at which point
// the socket-backed client and the host serving layer marshal their operations through here.
//
// FlatBuffers stays an implementation detail (it lives in the .cpp); callers see only these plain
// structs and free functions. Stream framing (length-prefixing messages on the socket) is a
// transport concern and lives with the socket send/recv helpers, not here.

// Direction of a device-memory access; mirrors wire::SimulationServerCommand in the schema.
enum class SimulationServerCommand : int8_t {
    Read = 0,
    Write = 1,
    GetDeviceInfo = 2,
    GetClusterDescriptor = 3,
};

// Which simulator the host runs; mirrors wire::SimulationBackendType. Served as part of the device
// identity so a client can build the matching device class without a local simulator build.
enum class SimulationBackendType : int8_t {
    TTSim = 0,
    Rtl = 1,
};

// A device-memory access request addressed by (core x/y, address, size).
struct SimulationServerRequest {
    SimulationServerCommand command = SimulationServerCommand::Read;
    uint32_t x = 0;
    uint32_t y = 0;
    uint64_t address = 0;
    uint32_t size = 0;
    // Bytes to write (length == size) for Write; empty for Read.
    std::vector<uint8_t> data;
};

// The response to a SimulationServerRequest.
struct SimulationServerResponse {
    // 0 on success; nonzero signals a host-side failure.
    int32_t status = 0;
    // Bytes read (length == size on success) for Read; empty for Write.
    std::vector<uint8_t> data;
};

// Device identity a host serves in reply to a GetDeviceInfo request; mirrors
// wire::SimulationServerDeviceInfo. Everything a client needs to build a matching SocDescriptor.
struct SimulationServerDeviceInfo {
    // 0 on success; nonzero signals a host-side failure (e.g. the host had no YAML to serve).
    int32_t status = 0;
    // tt::ARCH value of the served device.
    int32_t arch = 0;
    // Which simulator the host runs, so the client instantiates the matching device class.
    SimulationBackendType backend_type = SimulationBackendType::TTSim;
    // Full text of the host's SoC descriptor YAML file, so the client can build a matching one.
    std::string soc_descriptor_yaml;
    // Whether the host applies NOC translation.
    bool noc_translation_enabled = false;
    // Per-chip harvesting masks (logical-coordinate bitmasks; see HarvestingMasks).
    uint32_t tensix_harvesting_mask = 0;
    uint32_t dram_harvesting_mask = 0;
    uint32_t eth_harvesting_mask = 0;
    uint32_t l2cpu_harvesting_mask = 0;
    uint32_t pcie_harvesting_mask = 0;
};

// Cluster topology a host serves in reply to a GetClusterDescriptor request; mirrors
// wire::SimulationServerClusterDescriptor.
struct SimulationServerClusterDescriptor {
    // 0 on success; nonzero signals a host-side failure.
    int32_t status = 0;
    // The host's cluster-descriptor YAML text, so the client can rebuild the full ClusterDescriptor.
    // Empty when the host has no cluster descriptor (client falls back to a mock from the device info).
    std::string yaml;
};

// Serialize a message to a FlatBuffers payload.
std::vector<uint8_t> encode(const SimulationServerRequest& request);
std::vector<uint8_t> encode(const SimulationServerResponse& response);
std::vector<uint8_t> encode(const SimulationServerDeviceInfo& device_info);
std::vector<uint8_t> encode(const SimulationServerClusterDescriptor& cluster_descriptor);

// Parse a message back from a FlatBuffers payload. The buffer is verified against the schema
// before any field is read (it comes off a socket, so it may be malformed or truncated); a bad
// or empty buffer throws error::RuntimeError rather than risking an out-of-bounds read.
SimulationServerRequest decode_request(const uint8_t* data, size_t size);
SimulationServerResponse decode_response(const uint8_t* data, size_t size);
SimulationServerDeviceInfo decode_device_info(const uint8_t* data, size_t size);
SimulationServerClusterDescriptor decode_cluster_descriptor(const uint8_t* data, size_t size);
SimulationServerRequest decode_request(const std::vector<uint8_t>& bytes);
SimulationServerResponse decode_response(const std::vector<uint8_t>& bytes);
SimulationServerDeviceInfo decode_device_info(const std::vector<uint8_t>& bytes);
SimulationServerClusterDescriptor decode_cluster_descriptor(const std::vector<uint8_t>& bytes);

}  // namespace tt::umd
