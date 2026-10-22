// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <arpa/inet.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "simulation_device_generated.h"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/simulation/rtl_sim_communicator.hpp"
#include "tt-umd/simulation/rtl_sim_session.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/ip_layout.hpp"
#include "tt-umd/tt_device/rtl_simulation_tt_device.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/io_window_config.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

int free_port() {
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
    const bool ok = ::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                    ::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == 0;
    ::close(sock);
    EXPECT_TRUE(ok) << "could not reserve a local port";
    return ntohs(addr.sin_port);
}

// What the mock remote answers a read of the word at @p addr on core (@p x, @p y): unique per core
// and word, so a response handed to the wrong request is caught.
uint32_t expected_word(uint32_t x, uint32_t y, uint64_t addr) {
    return static_cast<uint32_t>(addr) ^ (x << 28) ^ (y << 24);
}

// The simulator side of one socket: dials the host and sends one message.
class MockRemote {
public:
    MockRemote(const std::string& address, DEVICE_COMMAND command) {
        nng_pair1_open(&socket_);
        int dialed = -1;
        for (int attempt = 0; attempt < 500 && (dialed = nng_dial(socket_, address.c_str(), nullptr, 0)) != 0;
             attempt++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_EQ(dialed, 0) << "could not dial " << address;
        flatbuffers::FlatBufferBuilder builder;
        const tt_vcs_core core(0, 0);
        builder.Finish(CreateDeviceRequestResponse(
            builder, command, builder.CreateVector(std::vector<uint32_t>{1, 0}), &core, 0, 0));
        EXPECT_EQ(nng_send(socket_, builder.GetBufferPointer(), builder.GetSize(), 0), 0);
    }

    ~MockRemote() { nng_close(socket_); }

    MockRemote(const MockRemote&) = delete;
    MockRemote& operator=(const MockRemote&) = delete;

    // Takes one message, waiting up to @p timeout_ms: answers a read with expected_word()s and counts
    // an exit in @p exits.
    void answer_one(int timeout_ms, std::atomic<int>& exits) {
        void* buf = nullptr;
        size_t size = 0;
        nng_socket_set_ms(socket_, NNG_OPT_RECVTIMEO, timeout_ms);
        if (nng_recv(socket_, &buf, &size, NNG_FLAG_ALLOC) != 0) {
            return;
        }
        const DeviceRequestResponse* request = GetDeviceRequestResponse(buf);
        if (request->command() == DEVICE_COMMAND_EXIT) {
            exits++;
        } else if (request->command() == DEVICE_COMMAND_READ) {
            const uint32_t x = request->core()->x();
            const uint32_t y = request->core()->y();
            std::vector<uint32_t> words(request->size() / sizeof(uint32_t));
            for (size_t i = 0; i < words.size(); i++) {
                words[i] = expected_word(x, y, request->address() + i * sizeof(uint32_t));
            }
            flatbuffers::FlatBufferBuilder builder;
            const tt_vcs_core core(x, y);
            builder.Finish(CreateDeviceRequestResponse(
                builder, DEVICE_COMMAND_READ, builder.CreateVector(words), &core, request->address(), request->size()));
            EXPECT_EQ(nng_send(socket_, builder.GetBufferPointer(), builder.GetSize(), 0), 0);
        }
        nng_free(buf, size);
    }

    std::string receive() {
        void* buf = nullptr;
        size_t size = 0;
        nng_socket_set_ms(socket_, NNG_OPT_RECVTIMEO, 5000);
        if (nng_recv(socket_, &buf, &size, NNG_FLAG_ALLOC) != 0) {
            return "";
        }
        std::string message(static_cast<char*>(buf), size);
        nng_free(buf, size);
        return message;
    }

private:
    nng_socket socket_;
};

class RtlSimSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / ("tt-umd-rtl-sim-session-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir_);
        // run.sh records the addresses it was launched with, to show they were exported before launch.
        std::ofstream(dir_ / "run.sh") << "#!/bin/sh\necho \"$NNG_SOCKET_ADDR_ut_a $NNG_SOCKET_ADDR_ut_b\" > \""
                                       << (dir_ / "launched_with").string() << "\"\n";
        std::filesystem::permissions(dir_ / "run.sh", std::filesystem::perms::owner_all);
        setenv("TT_SIMULATOR_LOCALHOST", "1", 1);
    }

    void TearDown() override {
        for (const std::string& name : names_) {
            unsetenv(("NNG_SOCKET_LOCAL_PORT_" + name).c_str());
            unsetenv(("NNG_SOCKET_ADDR_" + name).c_str());
        }
        unsetenv("TT_SIMULATOR_LOCALHOST");
        std::filesystem::remove_all(dir_);
    }

    // Pins socket @p name to a free local port and returns the address the remote dials. The address
    // itself is left for SimulationHost::init() to export, before run.sh is launched.
    std::string add_socket(const std::string& name) {
        const std::string port = std::to_string(free_port());
        setenv(("NNG_SOCKET_LOCAL_PORT_" + name).c_str(), port.c_str(), 1);
        names_.push_back(name);
        return "tcp://localhost:" + port;
    }

    std::filesystem::path dir_;
    std::vector<std::string> names_;
};

TEST_F(RtlSimSessionTest, AcksOnEverySocket) {
    const std::string address_a = add_socket("ut_a");
    const std::string address_b = add_socket("ut_b");
    RtlSimSession session(dir_, {"ut_a", "ut_b"});

    std::unique_ptr<MockRemote> remote_a;
    std::unique_ptr<MockRemote> remote_b;
    std::thread remotes([&] {
        remote_a = std::make_unique<MockRemote>(address_a, DEVICE_COMMAND_EXIT);
        remote_b = std::make_unique<MockRemote>(address_b, DEVICE_COMMAND_EXIT);
    });
    session.start(std::chrono::milliseconds(10'000));
    remotes.join();

    EXPECT_EQ(session.get_num_sockets(), 2);

    std::ifstream launched_with(dir_ / "launched_with");
    std::stringstream seen;
    seen << launched_with.rdbuf();
    EXPECT_EQ(seen.str(), address_a + " " + address_b + "\n");

    std::string to_a = "to a";
    std::string to_b = "to b";
    session.get_host(0).send_to_device(reinterpret_cast<uint8_t*>(to_a.data()), to_a.size());
    session.get_host(1).send_to_device(reinterpret_cast<uint8_t*>(to_b.data()), to_b.size());
    EXPECT_EQ(remote_a->receive(), "to a");
    EXPECT_EQ(remote_b->receive(), "to b");
}

TEST_F(RtlSimSessionTest, TimesOutWithoutAck) {
    add_socket("ut_a");
    RtlSimSession session(dir_, {"ut_a"});

    EXPECT_THROW(session.start(std::chrono::milliseconds(200)), std::exception);
}

TEST_F(RtlSimSessionTest, RejectsAWrongAck) {
    const std::string address = add_socket("ut_a");
    RtlSimSession session(dir_, {"ut_a"});

    std::unique_ptr<MockRemote> remote;
    std::thread dialer([&] { remote = std::make_unique<MockRemote>(address, DEVICE_COMMAND_READ); });
    EXPECT_THROW(session.start(std::chrono::milliseconds(10'000)), std::exception);
    dialer.join();
}

TEST_F(RtlSimSessionTest, RejectsDuplicateSocketNames) {
    EXPECT_THROW(RtlSimSession(dir_, {"ut_a", "ut_a"}), std::exception);
}

TEST_F(RtlSimSessionTest, RejectsAnInvalidSocketName) {
    add_socket("ut_a");
    RtlSimSession session(dir_, {"ut-a"});

    EXPECT_THROW(session.start(std::chrono::milliseconds(200)), std::exception);
}

TEST_F(RtlSimSessionTest, HandsOutHostsOnlyAfterStart) {
    const std::string address = add_socket("ut_a");
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{"ut_a"});
    EXPECT_THROW(session->get_host(0), std::exception);

    std::unique_ptr<MockRemote> remote;
    std::thread dialer([&] { remote = std::make_unique<MockRemote>(address, DEVICE_COMMAND_EXIT); });
    session->start(std::chrono::milliseconds(10'000));
    dialer.join();

    EXPECT_NO_THROW(session->get_host(0));
    EXPECT_THROW(session->start(std::chrono::milliseconds(200)), std::exception);

    // A socket handle reaches the same host and keeps the session alive.
    const RtlSimSocket socket(session, 0);
    EXPECT_EQ(&socket.get_host(), &session->get_host(0));
    EXPECT_THROW(RtlSimSocket(session, 1), std::exception);
    EXPECT_THROW(RtlSimSocket(nullptr, 0), std::exception);
}

TEST_F(RtlSimSessionTest, ServesWithoutLaunching) {
    const std::string address = add_socket("ut_a");
    RtlSimSession session(dir_, {"ut_a"}, /*launch_simulator=*/false);

    std::unique_ptr<MockRemote> remote;
    std::thread dialer([&] { remote = std::make_unique<MockRemote>(address, DEVICE_COMMAND_EXIT); });
    session.start(std::chrono::milliseconds(10'000));
    dialer.join();

    EXPECT_FALSE(std::filesystem::exists(dir_ / "launched_with"));
}

// The flat-address functions move only what the simulator moves: whole, aligned words.
TEST_F(RtlSimSessionTest, GlobalAccessRejectsPartialWords) {
    const std::string address = add_socket("ut_a");
    RtlSimSession session(dir_, {"ut_a"});
    std::unique_ptr<MockRemote> remote;
    std::thread dialer([&] { remote = std::make_unique<MockRemote>(address, DEVICE_COMMAND_EXIT); });
    session.start(std::chrono::milliseconds(10'000));
    dialer.join();

    RtlSimCommunicator communicator(session.get_host(0));
    uint32_t word = 0;
    EXPECT_THROW(communicator.global_write_words(0x1000, &word, 1), std::exception);
    EXPECT_THROW(communicator.global_write_words(0x1001, &word, 4), std::exception);
    EXPECT_THROW(communicator.global_read_words(0x1000, &word, 2), std::exception);
    EXPECT_THROW(communicator.global_read_words(0x1002, &word, 4), std::exception);
}

// Devices whose access points share a socket share its channel: each request gets its own response,
// however their threads interleave, and the remote is told to exit once, after the last of them is done.
TEST_F(RtlSimSessionTest, CommunicatorsShareASocket) {
    const std::string address = add_socket("ut_a");
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{"ut_a"}, /*launch_simulator=*/false);
    std::unique_ptr<MockRemote> remote;
    std::thread dialer([&] { remote = std::make_unique<MockRemote>(address, DEVICE_COMMAND_EXIT); });
    session->start(std::chrono::milliseconds(10'000));
    dialer.join();

    std::atomic<bool> serving{true};
    std::atomic<int> exits{0};
    std::thread server([&] {
        while (serving.load()) {
            remote->answer_one(50, exits);
        }
    });

    RtlSimCommunicator first(RtlSimSocket(session, 0));
    RtlSimCommunicator second(RtlSimSocket(session, 0));
    first.initialize();
    second.initialize();

    const auto read_all = [](RtlSimCommunicator& communicator, uint32_t x, int& mismatches) {
        for (uint64_t addr = 0x1000; addr < 0x1000 + 200 * 64; addr += 64) {
            std::array<uint32_t, 4> data{};
            communicator.tile_read_bytes(x, 0, addr, data.data(), sizeof(data));
            for (size_t i = 0; i < data.size(); i++) {
                mismatches += data[i] != expected_word(x, 0, addr + i * sizeof(uint32_t)) ? 1 : 0;
            }
        }
    };
    int first_mismatches = 0;
    int second_mismatches = 0;
    std::thread reader_a([&] { read_all(first, 0, first_mismatches); });
    std::thread reader_b([&] { read_all(second, 1, second_mismatches); });
    reader_a.join();
    reader_b.join();
    EXPECT_EQ(first_mismatches, 0);
    EXPECT_EQ(second_mismatches, 0);

    first.shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(exits.load(), 0) << "the remote was told to exit while a device still used the socket";

    second.shutdown();
    for (int attempt = 0; attempt < 100 && exits.load() == 0; attempt++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    serving.store(false);
    server.join();
    EXPECT_EQ(exits.load(), 1);
}

// A communicator attaches while its session is still being put together, and its requests work once
// the session has started. Sockets can be added by name only until then.
TEST_F(RtlSimSessionTest, CommunicatorsAttachBeforeStart) {
    const std::string address = add_socket("ut_a");
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{}, /*launch_simulator=*/false);
    RtlSimCommunicator communicator(RtlSimSocket(session, "ut_a"));
    communicator.initialize();

    std::unique_ptr<MockRemote> remote;
    std::thread dialer([&] { remote = std::make_unique<MockRemote>(address, DEVICE_COMMAND_EXIT); });
    session->start(std::chrono::milliseconds(10'000));
    dialer.join();
    EXPECT_THROW(RtlSimSocket(session, "ut_b"), std::exception);

    std::atomic<bool> serving{true};
    std::atomic<int> exits{0};
    std::thread server([&] {
        while (serving.load()) {
            remote->answer_one(50, exits);
        }
    });
    std::array<uint32_t, 4> data{};
    communicator.tile_read_bytes(1, 2, 0x1000, data.data(), sizeof(data));
    EXPECT_EQ(data, (std::array<uint32_t, 4>{0x1200'1000, 0x1200'1004, 0x1200'1008, 0x1200'100C}));

    communicator.shutdown();
    for (int attempt = 0; attempt < 100 && exits.load() == 0; attempt++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    serving.store(false);
    server.join();
    EXPECT_EQ(exits.load(), 1);
}

// A device with several access points talks over its first: only that socket joins the session, so
// the session starts with the remote dialing that one alone, and the device says goodbye on it.
TEST_F(RtlSimSessionTest, DeviceTalksOverItsFirstAccessPoint) {
    const std::string address = add_socket("ut_a");
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{}, /*launch_simulator=*/false);
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_simulation_1x3.yaml")));
    const IpLayoutDevice layout_device{IpDeviceId{0}, {{AccessPointId{0}, "ut_a"}, {AccessPointId{1}, "ut_b"}}};
    auto device = std::make_unique<RtlSimulationTTDevice>(session, layout_device, dir_, soc_descriptor, 0);
    EXPECT_EQ(session->get_num_sockets(), 1);

    std::unique_ptr<MockRemote> remote;
    std::thread dialer([&] { remote = std::make_unique<MockRemote>(address, DEVICE_COMMAND_EXIT); });
    session->start(std::chrono::milliseconds(10'000));
    dialer.join();

    device.reset();
    std::atomic<int> exits{0};
    remote->answer_one(5000, exits);
    EXPECT_EQ(exits.load(), 1);
}

// Fabric reads a device's access points and, with an ATT map named, a core's flat address through
// each. The query does no IO, so the session needn't start.
TEST_F(RtlSimSessionTest, DeviceGivesItsAccessPointsFlatAddresses) {
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{}, /*launch_simulator=*/false);
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_simulation_2x3.yaml")));
    const IpLayoutDevice layout_device{IpDeviceId{1}, {{AccessPointId{2}, "ut_a"}, {AccessPointId{3}, "ut_b"}}};
    setenv("TT_UMD_NOC_ATT", "horizon_2x3", 1);
    const RtlSimulationTTDevice device(session, layout_device, dir_, soc_descriptor, 0);
    unsetenv("TT_UMD_NOC_ATT");
    const CoreCoord tensix(0, 0, CoreType::TENSIX, CoordSystem::LITERAL);

    EXPECT_EQ(device.get_access_points(), (std::vector<AccessPointId>{AccessPointId{2}, AccessPointId{3}}));
    EXPECT_EQ(
        device.get_flat_address(AccessPointId{3}, tensix, WindowFlags::UnicastWrite, IoOrdering::Posted, 0x100),
        std::optional<uint64_t>(0x100'0000'0100));
    EXPECT_THROW(
        device.get_flat_address(AccessPointId{0}, tensix, WindowFlags::UnicastWrite, IoOrdering::Posted, 0x100),
        std::exception);
}

TEST_F(RtlSimSessionTest, DeviceWithoutAnAttMapHasNoFlatAddresses) {
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{}, /*launch_simulator=*/false);
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_simulation_2x3.yaml")));
    const RtlSimulationTTDevice device(session, {IpDeviceId{0}, {{AccessPointId{0}, "ut_a"}}}, dir_, soc_descriptor, 0);

    EXPECT_EQ(
        device.get_flat_address(
            AccessPointId{0},
            CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LITERAL),
            WindowFlags::Bidirectional,
            IoOrdering::Strict),
        std::nullopt);
}

// With a map that has a table per access point, each of a device's access points has its own flat
// addresses.
TEST_F(RtlSimSessionTest, AccessPointsOfADeviceHaveTheirOwnFlatAddresses) {
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{}, /*launch_simulator=*/false);
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_simulation_2x3.yaml")));
    const IpLayoutDevice layout_device{IpDeviceId{1}, {{AccessPointId{2}, "ut_a"}, {AccessPointId{3}, "ut_a"}}};
    setenv("TT_UMD_NOC_ATT", "horizon_x2", 1);
    const RtlSimulationTTDevice device(session, layout_device, dir_, soc_descriptor, 0);
    unsetenv("TT_UMD_NOC_ATT");
    const CoreCoord tensix(0, 0, CoreType::TENSIX, CoordSystem::LITERAL);

    EXPECT_EQ(
        device.get_flat_address(AccessPointId{2}, tensix, WindowFlags::UnicastWrite, IoOrdering::Posted, 0x100),
        std::optional<uint64_t>(0x20'4020'0100));
    EXPECT_EQ(
        device.get_flat_address(AccessPointId{3}, tensix, WindowFlags::UnicastWrite, IoOrdering::Posted, 0x100),
        std::optional<uint64_t>(0x21'4020'0100));
}

// horizon_x2_columns takes its tables in the order of the column layout's access points
// (docs/IP_LAYOUT.md), so each access point reaches its own column device.
class RtlSimSessionColumnLayoutTest : public RtlSimSessionTest,
                                      public ::testing::WithParamInterface<std::tuple<uint32_t, uint64_t>> {};

TEST_P(RtlSimSessionColumnLayoutTest, AccessPointReachesItsColumn) {
    const auto [access_point, address] = GetParam();
    auto session = std::make_shared<RtlSimSession>(dir_, std::vector<std::string>{}, /*launch_simulator=*/false);
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_simulation_2x3.yaml")));
    setenv("TT_UMD_NOC_ATT", "horizon_x2_columns", 1);
    const RtlSimulationTTDevice device(
        session, {IpDeviceId{0}, {{AccessPointId{access_point}, "ut_a"}}}, dir_, soc_descriptor, 0);
    unsetenv("TT_UMD_NOC_ATT");

    EXPECT_EQ(
        device.get_flat_address(
            AccessPointId{access_point},
            CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LITERAL),
            WindowFlags::UnicastWrite,
            IoOrdering::Posted,
            0x5'0000),
        std::optional<uint64_t>(address));
}

// Device 0 columns 0 and 1, then device 1 columns 0 and 1.
INSTANTIATE_TEST_SUITE_P(
    InLayoutOrder,
    RtlSimSessionColumnLayoutTest,
    ::testing::Values(
        std::make_tuple(0u, uint64_t{0x10'4005'0000}),
        std::make_tuple(1u, uint64_t{0x11'4005'0000}),
        std::make_tuple(2u, uint64_t{0x20'4005'0000}),
        std::make_tuple(3u, uint64_t{0x21'4005'0000})));

}  // namespace
