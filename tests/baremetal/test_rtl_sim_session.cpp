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

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "simulation_device_generated.h"
#include "umd/device/simulation/rtl_sim_communicator.hpp"
#include "umd/device/simulation/rtl_sim_session.hpp"

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

}  // namespace

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
