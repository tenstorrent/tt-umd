/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/multi_process_tt_sim_chip.hpp"
#include "umd/device/simulation/message_data.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<MultiProcessTTSimChip>(), "MultiProcessTTSimChip must be non-abstract.");

MultiProcessTTSimChip::MultiProcessTTSimChip(
    const std::filesystem::path& simulator_directory,
    SocDescriptor soc_descriptor,
    ClusterDescriptor* cluster_desc,
    ChipId chip_id) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id),
    architecture_impl_(architecture_implementation::create(soc_descriptor_.arch)) {

    if (!std::filesystem::exists(simulator_directory)) {
        TT_THROW("Simulator binary not found at: ", simulator_directory);
    }

    // Create process manager
    process_manager_ = std::make_unique<ProcessManager>(chip_id);

    // Start the child process
    process_manager_->start_child_process(simulator_directory, cluster_desc);
}

MultiProcessTTSimChip::~MultiProcessTTSimChip() {
    if (process_manager_) {
        process_manager_->stop_child_process();
    }
}

void MultiProcessTTSimChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);

    process_manager_->send_message_with_response(MessageType::START_DEVICE, nullptr, 0, nullptr, 0);
}

void MultiProcessTTSimChip::close_device() {
    std::lock_guard<std::mutex> lock(device_lock);

    process_manager_->send_message_with_response(MessageType::CLOSE_DEVICE, nullptr, 0, nullptr, 0);
}

void MultiProcessTTSimChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, l1_dest, core.str());
    send_write_message(core, src, l1_dest, size);
}

void MultiProcessTTSimChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    // Note: Reads no longer clock the device in multi-process implementation
    send_read_message(core, l1_src, size, dest);
}


void MultiProcessTTSimChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    send_tensix_risc_reset_message(translated_core, soft_resets);
}

void MultiProcessTTSimChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    Chip::send_tensix_risc_reset(soft_resets);
}

void MultiProcessTTSimChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    send_assert_risc_reset_message(core, selected_riscs);
}

void MultiProcessTTSimChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    send_deassert_risc_reset_message(core, selected_riscs, staggered_start);
}

bool MultiProcessTTSimChip::connect_eth_sockets() {
    bool result = false;
    process_manager_->send_message_with_response(MessageType::CONNECT_ETH_SOCKETS, nullptr, 0, &result, sizeof(bool));
    return result;
}

void MultiProcessTTSimChip::send_write_message(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    WriteMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.l1_dest = l1_dest;
    msg_data.size = size;

    // Send message header and data directly to avoid copying, and wait for response
    process_manager_->send_message_with_data_and_response(MessageType::WRITE_TO_DEVICE, &msg_data, sizeof(WriteMessageData), src, size);
}

void MultiProcessTTSimChip::send_read_message(CoreCoord core, uint64_t l1_src, uint32_t size, void* dest) {
    ReadMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.l1_src = l1_src;
    msg_data.size = size;

    process_manager_->send_message_with_response(MessageType::READ_FROM_DEVICE, &msg_data, sizeof(ReadMessageData), dest, size);
}


void MultiProcessTTSimChip::send_tensix_risc_reset_message(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    TensixResetMessageData msg_data;
    msg_data.translated_core = translated_core;
    msg_data.soft_resets = soft_resets;

    process_manager_->send_message_with_response(MessageType::SEND_TENSIX_RISC_RESET, &msg_data, sizeof(TensixResetMessageData), nullptr, 0);
}

void MultiProcessTTSimChip::send_assert_risc_reset_message(CoreCoord core, const RiscType selected_riscs) {
    AssertResetMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.selected_riscs = selected_riscs;

    process_manager_->send_message_with_response(MessageType::ASSERT_RISC_RESET, &msg_data, sizeof(AssertResetMessageData), nullptr, 0);
}

void MultiProcessTTSimChip::send_deassert_risc_reset_message(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    DeassertResetMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.selected_riscs = selected_riscs;
    msg_data.staggered_start = staggered_start;

    process_manager_->send_message_with_response(MessageType::DEASSERT_RISC_RESET, &msg_data, sizeof(DeassertResetMessageData), nullptr, 0);
}

}  // namespace tt::umd
