// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <flatbuffers/flatbuffer_builder.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "tt-umd/simulation/rtl_sim_communicator.hpp"
#include "tt-umd/simulation/simulation_host.hpp"

namespace tt::umd {

/**
 * The host side of one socket, shared by every RtlSimCommunicator that talks over it. Several devices
 * may share a socket (their access points name the same host_access), and the remote neither tags
 * its responses nor tells devices apart, so everything per socket lives here: one thread receives all
 * the remote sends, responses wait in one queue, and one request is in flight at a time.
 */
class RtlSimChannel {
public:
    explicit RtlSimChannel(SimulationHost &host);

    ~RtlSimChannel();

    RtlSimChannel(const RtlSimChannel &) = delete;
    RtlSimChannel &operator=(const RtlSimChannel &) = delete;

    SimulationHost &get_host() { return host_; }

    /** A communicator starts using the channel. The first one starts the receive thread. */
    void attach();

    /**
     * A communicator stops using the channel. The last one stops the receive thread and, with
     * @p send_exit, tells the remote to exit.
     */
    void detach(bool send_exit);

    /**
     * Held for a whole request: from its send to its response, and across the read and write of a
     * sub-word write. Communicators on the socket take turns through it.
     */
    std::mutex &request_mutex() { return request_mutex_; }

    /** Send one message to the remote. */
    void send(const flatbuffers::FlatBufferBuilder &message);

    /** The next response the receive thread queued; empty once the thread has stopped. */
    RtlSimCommunicator::ReceivedMessage wait_for_response();

    /**
     * Where the remote's host memory requests go. The socket has one host memory, so the first
     * communicator to set them keeps them; a later one is told so and ignored.
     */
    void set_ram_callbacks(RtlSimCommunicator::RamWriteCallback write_cb, RtlSimCommunicator::RamReadCallback read_cb);

private:
    void receive_loop();
    void handle_ram_write(const void *notification);
    void handle_ram_read(const void *notification);
    void stop_receiving();

    SimulationHost &host_;

    // Serializes sends: requests, and the receive thread's answers to host memory reads.
    std::mutex send_mutex_;
    std::mutex request_mutex_;

    std::mutex users_mutex_;
    size_t users_ = 0;

    std::thread receive_thread_;
    std::atomic<bool> receiving_{false};

    std::queue<RtlSimCommunicator::ReceivedMessage> responses_;
    std::mutex responses_mutex_;
    std::condition_variable responses_cv_;

    std::mutex callbacks_mutex_;
    RtlSimCommunicator::RamWriteCallback ram_write_callback_;
    RtlSimCommunicator::RamReadCallback ram_read_callback_;
};

}  // namespace tt::umd
