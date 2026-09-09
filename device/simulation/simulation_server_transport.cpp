// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_server_transport.hpp"

#include <fmt/format.h>

#include <array>
#include <asio.hpp>
#include <system_error>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

using stream_protocol = asio::local::stream_protocol;

constexpr size_t FRAME_HEADER_SIZE = sizeof(uint32_t);

// Upper bound on a single framed payload. The length prefix comes off an untrusted socket, so a
// garbled or malicious peer could otherwise advertise up to 4 GiB and make recv_framed() attempt a
// huge allocation. Cap it well below that at a size no legitimate device-memory transfer reaches;
// anything larger is rejected before allocating.
constexpr uint32_t MAX_FRAME_PAYLOAD_SIZE = 1u << 30;  // 1 GiB

// asio::write/asio::read transfer exactly n bytes or set ec (short transfer on a closed peer, etc.);
// convert any failure to a RuntimeError. asio sets MSG_NOSIGNAL on the send, so a closed peer is an
// error here, never a SIGPIPE.
void write_exact(stream_protocol::socket& socket, const uint8_t* data, size_t n, const char* what) {
    std::error_code ec;
    asio::write(socket, asio::buffer(data, n), ec);
    if (ec) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Simulation server transport failed to write {}: {}", what, ec.message()));
    }
}

void read_exact(stream_protocol::socket& socket, uint8_t* data, size_t n, const char* what) {
    std::error_code ec;
    asio::read(socket, asio::buffer(data, n), ec);
    if (!ec) {
        return;
    }
    // A clean peer close (EOF) or reset means the remote end went away -- for a client blocked in
    // transact(), that is most often the host being stopped. Surface a distinct, clearly-worded
    // error so callers can tell "the server stopped" apart from a genuine I/O failure and unwind
    // gracefully. (On the host side, this is also the normal end-of-session when a client
    // disconnects, and the serving thread already treats it as such.)
    if (ec == asio::error::eof || ec == asio::error::connection_reset) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Simulation server transport peer closed the connection while reading {} -- the remote "
                "end (e.g. a stopped simulation host) has gone away",
                what));
    }
    UMD_THROW(
        error::RuntimeError, fmt::format("Simulation server transport failed to read {}: {}", what, ec.message()));
}

}  // namespace

void send_framed(stream_protocol::socket& socket, const std::vector<uint8_t>& payload) {
    // Bounded so the length always fits the uint32_t prefix and matches what recv_framed() will
    // accept; anything larger is a programming error rather than a truncation.
    UMD_ASSERT(
        payload.size() <= MAX_FRAME_PAYLOAD_SIZE,
        error::RuntimeError,
        fmt::format(
            "Simulation server transport payload too large to frame ({} bytes, max {})",
            payload.size(),
            MAX_FRAME_PAYLOAD_SIZE));
    const uint32_t length = static_cast<uint32_t>(payload.size());
    const std::array<uint8_t, FRAME_HEADER_SIZE> header = {
        static_cast<uint8_t>(length & 0xFF),
        static_cast<uint8_t>((length >> 8) & 0xFF),
        static_cast<uint8_t>((length >> 16) & 0xFF),
        static_cast<uint8_t>((length >> 24) & 0xFF),
    };
    // Write the header and payload separately so a large payload is not copied into a combined
    // frame buffer first.
    write_exact(socket, header.data(), header.size(), "frame header");
    if (!payload.empty()) {
        write_exact(socket, payload.data(), payload.size(), "frame payload");
    }
}

std::vector<uint8_t> recv_framed(stream_protocol::socket& socket) {
    std::array<uint8_t, FRAME_HEADER_SIZE> header;
    read_exact(socket, header.data(), header.size(), "frame header");
    const uint32_t length = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8) |
                            (static_cast<uint32_t>(header[2]) << 16) | (static_cast<uint32_t>(header[3]) << 24);

    // Reject an implausible length from the untrusted prefix before allocating, so a bad peer
    // can't drive a multi-GB allocation (or std::bad_alloc).
    UMD_ASSERT(
        length <= MAX_FRAME_PAYLOAD_SIZE,
        error::RuntimeError,
        fmt::format(
            "Simulation server transport received an oversized frame ({} bytes, max {})",
            length,
            MAX_FRAME_PAYLOAD_SIZE));

    std::vector<uint8_t> payload(length);
    if (length > 0) {
        read_exact(socket, payload.data(), length, "frame payload");
    }
    return payload;
}

}  // namespace tt::umd
