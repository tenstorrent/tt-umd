/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/multi_process_tt_sim_chip.hpp"

#include <filesystem>
#include <mutex>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "message_data.hpp"
#include "process_manager.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<MultiProcessTTSimChip>(), "MultiProcessTTSimChip must be non-abstract.");

MultiProcessTTSimChip::MultiProcessTTSimChip(
    const std::filesystem::path& simulator_directory,
    SocDescriptor soc_descriptor,
    ClusterDescriptor* cluster_desc,
    ChipId chip_id) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id) {
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
    WriteMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.l1_dest = l1_dest;
    msg_data.size = size;
    process_manager_->send_message_with_data_and_response(
        MessageType::WRITE_TO_DEVICE, &msg_data, sizeof(WriteMessageData), src, size);
}

void MultiProcessTTSimChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    ReadMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.l1_src = l1_src;
    msg_data.size = size;
    process_manager_->send_message_with_response(
        MessageType::READ_FROM_DEVICE, &msg_data, sizeof(ReadMessageData), dest, size);
}

void MultiProcessTTSimChip::send_tensix_risc_reset(
    tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    TensixResetMessageData msg_data;
    msg_data.translated_core = translated_core;
    msg_data.soft_resets = soft_resets;
    process_manager_->send_message_with_response(
        MessageType::SEND_TENSIX_RISC_RESET, &msg_data, sizeof(TensixResetMessageData), nullptr, 0);
}

void MultiProcessTTSimChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    Chip::send_tensix_risc_reset(soft_resets);
}

void MultiProcessTTSimChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    AssertResetMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.selected_riscs = selected_riscs;
    process_manager_->send_message_with_response(
        MessageType::ASSERT_RISC_RESET, &msg_data, sizeof(AssertResetMessageData), nullptr, 0);
}

void MultiProcessTTSimChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    DeassertResetMessageData msg_data;
    msg_data.translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    msg_data.selected_riscs = selected_riscs;
    msg_data.staggered_start = staggered_start;

    process_manager_->send_message_with_response(
        MessageType::DEASSERT_RISC_RESET, &msg_data, sizeof(DeassertResetMessageData), nullptr, 0);
}

bool MultiProcessTTSimChip::connect_eth_links() {
    std::lock_guard<std::mutex> lock(device_lock);
    bool result = false;
    process_manager_->send_message_with_response(MessageType::CONNECT_ETH_LINKS, nullptr, 0, &result, sizeof(bool));
    return result;
}

}  // namespace tt::umd
