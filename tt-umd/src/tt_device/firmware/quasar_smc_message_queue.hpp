// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tt::umd {

class DeviceProtocol;

/**
 * The message queue an SMC firmware in a Quasar package publishes for the host.
 *
 * A ring of four request and four response slots, found through the pointer the firmware leaves in
 * the SMC's cold SCRATCH[3] register. The host reaches the SMC by flat system physical address
 * alone, so every access is a single 32-bit control access over NOC0 with no core coordinate.
 *
 * Nothing on the device arbitrates between host users of the queue, so the caller serializes every
 * sender to the same SMC, across threads and processes.
 */
class QuasarSmcMessageQueue {
public:
    /** Words in a request or a response: the header word, then seven payload words. */
    static constexpr size_t MESSAGE_WORDS = 8;

    /**
     * @param device_protocol Issues the accesses. Non-owning; must outlive this queue.
     * @param smc_base System physical address of the target SMC's register block.
     */
    QuasarSmcMessageQueue(DeviceProtocol* device_protocol, uint64_t smc_base);

    /**
     * Sends one request and waits for the firmware's response.
     *
     * The queue is looked up on every call, so a call made before the firmware has published it
     * fails rather than writing through a null pointer. Responses already waiting in the ring when
     * the call starts, such as one an earlier call gave up on, are discarded rather than returned.
     * A response to an earlier call that timed out can still arrive after that point and be
     * returned in place of this call's own.
     *
     * @param header Request word 0, passed to the firmware unchanged.
     * @param args Request words 1 to 7, in order; words not given are sent as zero.
     * @param timeout Bounds the wait for a free request slot and the response together. Zero waits
     *        without limit.
     * @returns The response's eight words as the firmware wrote them.
     * @throws error::RuntimeError if more than seven args are given, if the firmware has not
     *         published its queue, or if a wait times out.
     */
    std::array<uint32_t, MESSAGE_WORDS> send_message(
        uint32_t header, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout);

private:
    DeviceProtocol* device_protocol_;
    uint64_t smc_base_;
};

}  // namespace tt::umd
