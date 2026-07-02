// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <asio/local/stream_protocol.hpp>
#include <cstdint>
#include <vector>

namespace tt::umd {

// Framed message transport over a connected UNIX-domain stream socket.
//
// A SOCK_STREAM socket has no message boundaries, so each message is length-prefixed on the wire:
// a 4-byte little-endian payload length, then the payload. send_framed() writes one framed
// message; recv_framed() reads exactly one back, blocking until the whole payload has arrived.
// This is what lets the protocol messages (simulation_server_protocol.hpp) travel intact between a
// UMD client and a simulation host.
//
// I/O goes through asio::read/asio::write on the connected socket: they transfer exactly the
// requested byte count (or fail), and on Linux asio sets MSG_NOSIGNAL, so writing to a closed peer
// surfaces as a thrown error rather than raising SIGPIPE. The socket must be in blocking mode (its
// owner clears any non-blocking flag asio leaves after connect/accept). Payloads are opaque here;
// encoding/decoding is the protocol layer's job.
//
// Every failure (peer closed mid-message, oversized frame, socket error) throws error::RuntimeError.

// Length-prefix and write one whole message to the socket.
void send_framed(asio::local::stream_protocol::socket& socket, const std::vector<uint8_t>& payload);

// Read exactly one length-prefixed message from the socket, blocking until it is complete.
std::vector<uint8_t> recv_framed(asio::local::stream_protocol::socket& socket);

}  // namespace tt::umd
