// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <vector>

#include "umd/device/simulation/simulation_server_protocol.hpp"

using namespace tt::umd;

// A read request carries the access geometry and no payload; encode -> decode reproduces it.
TEST(SimulationServerProtocol, ReadRequestRoundTrip) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::Read;
    request.x = 3;
    request.y = 5;
    request.address = 0x1000'2000'3000'4000ULL;
    request.size = 256;

    const SimulationServerRequest decoded = decode_request(encode(request));

    EXPECT_EQ(decoded.command, SimulationServerCommand::Read);
    EXPECT_EQ(decoded.x, request.x);
    EXPECT_EQ(decoded.y, request.y);
    EXPECT_EQ(decoded.address, request.address);
    EXPECT_EQ(decoded.size, request.size);
    EXPECT_TRUE(decoded.data.empty());
}

// A write request carries its payload; encode -> decode reproduces geometry and bytes.
TEST(SimulationServerProtocol, WriteRequestRoundTrip) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::Write;
    request.x = 1;
    request.y = 2;
    request.address = 0xDEAD'BEEFULL;
    request.data = {0x11, 0x22, 0x33, 0x44, 0x55};
    request.size = static_cast<uint32_t>(request.data.size());

    const SimulationServerRequest decoded = decode_request(encode(request));

    EXPECT_EQ(decoded.command, SimulationServerCommand::Write);
    EXPECT_EQ(decoded.x, request.x);
    EXPECT_EQ(decoded.y, request.y);
    EXPECT_EQ(decoded.address, request.address);
    EXPECT_EQ(decoded.size, request.size);
    EXPECT_EQ(decoded.data, request.data);
}

// A read response carries the bytes read and a success status.
TEST(SimulationServerProtocol, ReadResponseRoundTrip) {
    SimulationServerResponse response;
    response.status = 0;
    response.data = {0xAA, 0xBB, 0xCC};

    const SimulationServerResponse decoded = decode_response(encode(response));

    EXPECT_EQ(decoded.status, 0);
    EXPECT_EQ(decoded.data, response.data);
}

// A failed response carries a nonzero status and no payload.
TEST(SimulationServerProtocol, ErrorResponseRoundTrip) {
    SimulationServerResponse response;
    response.status = -5;

    const SimulationServerResponse decoded = decode_response(encode(response));

    EXPECT_EQ(decoded.status, -5);
    EXPECT_TRUE(decoded.data.empty());
}

// An empty buffer can never be a valid message; decoding it fails rather than reading nothing.
TEST(SimulationServerProtocol, DecodeEmptyBufferThrows) {
    const std::vector<uint8_t> empty;
    EXPECT_THROW(decode_request(empty), std::exception);
    EXPECT_THROW(decode_response(empty), std::exception);
}

// A truncated payload (a valid message cut short, as a partial socket read would produce) fails
// verification instead of reading out of bounds.
TEST(SimulationServerProtocol, DecodeTruncatedBufferThrows) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::Read;
    request.size = 64;
    std::vector<uint8_t> encoded = encode(request);
    ASSERT_GT(encoded.size(), 4u);
    encoded.resize(encoded.size() / 2);  // chop it in half

    EXPECT_THROW(decode_request(encoded), std::exception);
}

// GET_DEVICE_INFO round-trips arch, YAML text, and harvesting masks.
TEST(SimulationServerProtocol, DeviceInfoSerializationRoundTrip) {
    SimulationServerDeviceInfo info;
    info.status = 0;
    info.arch = 2;  // arbitrary tt::ARCH value
    info.backend_type = SimulationBackendType::Rtl;
    info.soc_descriptor_yaml = "arch: wormhole_b0\ngrid: [10, 12]\n";
    info.noc_translation_enabled = false;
    info.tensix_harvesting_mask = 0x5;
    info.dram_harvesting_mask = 0x0;
    info.eth_harvesting_mask = 0x120;
    info.l2cpu_harvesting_mask = 0x0;
    info.pcie_harvesting_mask = 0x3;

    const SimulationServerDeviceInfo decoded = decode_device_info(encode(info));

    EXPECT_EQ(decoded.status, info.status);
    EXPECT_EQ(decoded.arch, info.arch);
    EXPECT_EQ(decoded.backend_type, info.backend_type);
    EXPECT_EQ(decoded.soc_descriptor_yaml, info.soc_descriptor_yaml);
    EXPECT_EQ(decoded.noc_translation_enabled, info.noc_translation_enabled);
    EXPECT_EQ(decoded.tensix_harvesting_mask, info.tensix_harvesting_mask);
    EXPECT_EQ(decoded.dram_harvesting_mask, info.dram_harvesting_mask);
    EXPECT_EQ(decoded.eth_harvesting_mask, info.eth_harvesting_mask);
    EXPECT_EQ(decoded.l2cpu_harvesting_mask, info.l2cpu_harvesting_mask);
    EXPECT_EQ(decoded.pcie_harvesting_mask, info.pcie_harvesting_mask);
}

// The GetDeviceInfo command survives a request round-trip.
TEST(SimulationServerProtocol, GetDeviceInfoRequestRoundTrip) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::GetDeviceInfo;
    const SimulationServerRequest decoded = decode_request(encode(request));
    EXPECT_EQ(decoded.command, SimulationServerCommand::GetDeviceInfo);
}

// The cluster-descriptor message round-trips its status and YAML text.
TEST(SimulationServerProtocol, ClusterDescriptorRoundTrip) {
    SimulationServerClusterDescriptor cluster_descriptor;
    cluster_descriptor.status = 0;
    cluster_descriptor.yaml = "chips:\n  0: [0, 0, 0, 0]\nethernet_connections: []\n";

    const SimulationServerClusterDescriptor decoded = decode_cluster_descriptor(encode(cluster_descriptor));

    EXPECT_EQ(decoded.status, cluster_descriptor.status);
    EXPECT_EQ(decoded.yaml, cluster_descriptor.yaml);
}

// An empty YAML (the host has no cluster descriptor) round-trips as empty, not null.
TEST(SimulationServerProtocol, ClusterDescriptorEmptyYamlRoundTrip) {
    SimulationServerClusterDescriptor cluster_descriptor;  // status 0, empty yaml
    const SimulationServerClusterDescriptor decoded = decode_cluster_descriptor(encode(cluster_descriptor));
    EXPECT_EQ(decoded.status, 0);
    EXPECT_TRUE(decoded.yaml.empty());
}

// The GetClusterDescriptor command survives a request round-trip.
TEST(SimulationServerProtocol, GetClusterDescriptorRequestRoundTrip) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::GetClusterDescriptor;
    const SimulationServerRequest decoded = decode_request(encode(request));
    EXPECT_EQ(decoded.command, SimulationServerCommand::GetClusterDescriptor);
}

// The Shutdown command survives a request round-trip (its reply is a plain SimulationServerResponse).
TEST(SimulationServerProtocol, ShutdownRequestRoundTrip) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::Shutdown;
    const SimulationServerRequest decoded = decode_request(encode(request));
    EXPECT_EQ(decoded.command, SimulationServerCommand::Shutdown);
}
