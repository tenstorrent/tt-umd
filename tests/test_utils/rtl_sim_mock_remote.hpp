// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <arpa/inet.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "simulation_device_generated.h"

namespace tt::umd::test_utils {

// A loopback port nothing listens on, to pin an RTL simulation socket to (NNG_SOCKET_LOCAL_PORT_<name>).
inline int free_local_port() {
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

// What MockRemote answers a read of the word at @p addr on core (@p x, @p y): unique per core and word, so a response
// handed to the wrong request is caught.
inline uint32_t expected_word(uint32_t x, uint32_t y, uint64_t addr) {
    return static_cast<uint32_t>(addr) ^ (x << 28) ^ (y << 24);
}

// The RTL simulator's side of one socket: dials the host and sends one message (DEVICE_COMMAND_EXIT is the ack the
// host waits for).
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

    // Takes one message, waiting up to @p timeout_ms: answers a read with expected_word()s and counts an exit in
    // @p exits.
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

}  // namespace tt::umd::test_utils
