// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/blackhole_arc_message_queue.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

BlackholeArcMessageQueue::BlackholeArcMessageQueue(
    DeviceProtocol* device_protocol,
    BlackholeArcApb* arc_apb,
    const uint64_t base_address,
    const uint64_t size,
    const bool noc_translation_enabled) :
    base_address(base_address),
    size(size),
    device_protocol(device_protocol),
    arc_apb(arc_apb),
    noc_translation_enabled(noc_translation_enabled) {}

uint32_t BlackholeArcMessageQueue::send_message(
    const ArcMessageType message_type,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::send_message is not implemented yet");
}

std::unique_ptr<BlackholeArcMessageQueue> BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
    DeviceProtocol* device_protocol,
    JtagInterface* jtag_interface,
    BlackholeArcApb* arc_apb,
    const bool noc_translation_enabled,
    const size_t queue_index,
    const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::get_blackhole_arc_message_queue is not implemented yet");
}

void BlackholeArcMessageQueue::push_request(
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len>& request,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::push_request is not implemented yet");
}

std::array<uint32_t, BlackholeArcMessageQueue::entry_len> BlackholeArcMessageQueue::pop_response(
    const std::chrono::milliseconds timeout_ms, const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::pop_response is not implemented yet");
}

void BlackholeArcMessageQueue::read_words(uint32_t* data, size_t num_words, size_t offset, const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::read_words is not implemented yet");
}

uint32_t BlackholeArcMessageQueue::read_word(size_t offset, const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::read_word is not implemented yet");
}

void BlackholeArcMessageQueue::write_words(uint32_t* data, size_t num_words, size_t offset, const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::write_words is not implemented yet");
}

void BlackholeArcMessageQueue::trigger_fw_int(const NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::trigger_fw_int is not implemented yet");
}

tt_xy_pair BlackholeArcMessageQueue::get_arc_core(const NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "BlackholeArcMessageQueue::get_arc_core is not implemented yet");
}

}  // namespace tt::umd
