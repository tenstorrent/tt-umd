// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "umd/device/simulation/simulation_server_api.hpp"
#include "umd/device/simulation/simulation_socket.hpp"
#include "umd/device/simulation/simulation_topology_discovery.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt::umd;

namespace {

int connect_fd(const std::filesystem::path& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

constexpr uint8_t CMD_WRITE = 0;  // DEVICE_COMMAND values from simulation_device.fbs.
constexpr uint8_t CMD_READ = 1;

}  // namespace

// Integration: with no live host, discovery creates a host device that binds + exposes its
// socket, and tears it down with the device. Requires TT_UMD_SIMULATOR.
TEST(SimulationTopologyDiscovery, CreatesHostDeviceAndExposesSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket = SimulationSocket::default_socket_path(0);
    std::filesystem::remove(socket);

    SimulationTopologyDiscoveryOptions options;
    options.simulator_directory = simulator_path;

    {
        auto devices = SimulationTopologyDiscovery::discover(options);
        ASSERT_EQ(devices.size(), 1u);
        ASSERT_NE(devices.at(0), nullptr);
        EXPECT_TRUE(std::filesystem::exists(socket));  // host exposed it
    }

    EXPECT_FALSE(std::filesystem::exists(socket));  // torn down with the device
}

// Integration: the discovered host serves the API over its socket — a raw protocol client
// attaches, advances execution, writes a word, and reads it back. Requires TT_UMD_SIMULATOR.
TEST(SimulationTopologyDiscovery, HostServesApiOverSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket = SimulationSocket::default_socket_path(0);
    std::filesystem::remove(socket);

    SimulationTopologyDiscoveryOptions options;
    options.simulator_directory = simulator_path;
    auto devices = SimulationTopologyDiscovery::discover(options);
    ASSERT_EQ(devices.size(), 1u);

    const SocDescriptor& soc = devices.at(0)->get_soc_descriptor();
    const tt_xy_pair core =
        soc.translate_coord_to(soc.get_cores(tt::CoreType::TENSIX).at(0), tt::CoordSystem::TRANSLATED);

    const int fd = connect_fd(socket);
    ASSERT_GE(fd, 0);

    Message attach;
    attach.type = MessageType::AttachRequest;
    ASSERT_TRUE(send_framed(fd, encode(attach)));
    auto attach_resp = recv_framed(fd);
    ASSERT_TRUE(attach_resp.has_value());
    EXPECT_EQ(decode(*attach_resp).type, MessageType::AttachResponse);

    Message write;
    write.type = MessageType::DeviceOp;
    write.op.command = CMD_WRITE;
    write.op.endpoint = {static_cast<uint32_t>(core.x), static_cast<uint32_t>(core.y)};
    write.op.address = 0x100;
    write.op.size = 4;
    write.op.data = {0xcafebabe};
    ASSERT_TRUE(send_framed(fd, encode(write)));
    ASSERT_TRUE(recv_framed(fd).has_value());

    Message read;
    read.type = MessageType::DeviceOp;
    read.op.command = CMD_READ;
    read.op.endpoint = write.op.endpoint;
    read.op.address = 0x100;
    read.op.size = 4;
    ASSERT_TRUE(send_framed(fd, encode(read)));
    auto read_resp = recv_framed(fd);
    ASSERT_TRUE(read_resp.has_value());
    const Message out = decode(*read_resp);
    ASSERT_FALSE(out.op.data.empty());
    EXPECT_EQ(out.op.data[0], 0xcafebabeu);

    ::close(fd);
}
