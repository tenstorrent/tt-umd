// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/simulation/rtl_sim_communicator.hpp"

#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/vector.h>
#include <fmt/format.h>
#include <nng/nng.h>

#include <cstring>
#include <exception>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "common/utils.hpp"
#include "simulation/rtl_sim_channel.hpp"
#include "simulation/word_access.hpp"
#include "simulation_device_generated.h"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

/**
 * Create a flatbuffer for device communication.
 */
inline flatbuffers::FlatBufferBuilder create_flatbuffer(
    DEVICE_COMMAND rw, const std::vector<uint32_t> &vec, tt_xy_pair core_, uint64_t addr, uint64_t size_ = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    auto core = tt_vcs_core(core_.x, core_.y);
    uint64_t size = (size_ == 0 ? vec.size() * sizeof(uint32_t) : size_);
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, &core, addr, size);
    builder.Finish(device_cmd);
    return builder;
}

inline flatbuffers::FlatBufferBuilder create_flatbuffer(DEVICE_COMMAND rw, tt_xy_pair core) {
    return create_flatbuffer(rw, std::vector<uint32_t>(1, 0), core, 0);
}

/**
 * Create a flatbuffer for an access carrying no destination coordinate.
 *
 * The core field is left out rather than zeroed: an absent struct reads back as null, so a
 * simulator that tried to route on the coordinate would fault instead of targeting tile (0, 0).
 */
inline flatbuffers::FlatBufferBuilder create_global_flatbuffer(
    DEVICE_COMMAND rw, const std::vector<uint32_t> &vec, uint64_t addr, uint64_t size_ = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    uint64_t size = (size_ == 0 ? vec.size() * sizeof(uint32_t) : size_);
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, nullptr, addr, size);
    builder.Finish(device_cmd);
    return builder;
}

}  // namespace

RtlSimCommunicator::RtlSimCommunicator(const std::filesystem::path &simulator_directory) :
    simulator_directory_(simulator_directory) {
    if (!std::filesystem::exists(simulator_directory_)) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Simulator directory not found at: {}", simulator_directory_.string()));
    }
    owned_session_ = std::make_unique<RtlSimSession>(simulator_directory_, std::vector<std::string>{""});
    channel_ = owned_session_->get_channel(0);
}

RtlSimCommunicator::RtlSimCommunicator(SimulationHost &host) : channel_(std::make_shared<RtlSimChannel>(host)) {}

RtlSimCommunicator::RtlSimCommunicator(const RtlSimSocket &socket) : socket_(socket), channel_(socket.get_channel()) {}

RtlSimCommunicator::~RtlSimCommunicator() {
    if (attached_) {
        channel_->detach(/*send_exit=*/false);
    }
}

SimulationHost &RtlSimCommunicator::get_host() { return channel_->get_host(); }

void RtlSimCommunicator::initialize() {
    log_info(tt::LogEmulationDriver, "Initializing RTL simulation communicator");

    if (owned_session_ != nullptr) {
        owned_session_->start();
    }

    channel_->attach();
    attached_ = true;
}

void RtlSimCommunicator::shutdown() {
    // The last communicator on the socket stops its receive thread and tells the remote to exit.
    if (attached_) {
        attached_ = false;
        channel_->detach(/*send_exit=*/true);
    }
}

void RtlSimCommunicator::read_words(AccessKind kind, tt_xy_pair core, uint64_t addr, void *data, uint32_t size) {
    const char *name = kind == AccessKind::kGlobal ? "global" : (kind == AccessKind::kSmn ? "SMN" : "tile");
    if (kind == AccessKind::kGlobal) {
        channel_->send(create_global_flatbuffer(DEVICE_COMMAND_GLOBAL_READ, {0}, addr, size));
    } else {
        const DEVICE_COMMAND command = kind == AccessKind::kSmn ? DEVICE_COMMAND_SMN_READ : DEVICE_COMMAND_READ;
        channel_->send(create_flatbuffer(command, {0}, core, addr, size));
    }

    // The channel's receive thread queues the response; the request lock makes it this read's.
    auto msg = channel_->wait_for_response();
    if (msg.data == nullptr || msg.size == 0) {
        UMD_THROW(
            error::RuntimeError, "Failed to receive response from device - notification thread may have stopped.");
    }

    auto rd_resp_buf = GetDeviceRequestResponse(msg.data);
    if (rd_resp_buf->data() == nullptr || rd_resp_buf->data()->size() == 0) {
        // The remote answers a failed read with an EMPTY payload (its exception path); it is not a
        // response we can consume, and memcpy from a zero-length vector would fault on nullptr.
        const int resp_cmd = static_cast<int>(rd_resp_buf->command());
        const uint64_t resp_addr = rd_resp_buf->address();
        nng_free(msg.data, msg.size);
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "{} read: the simulator returned an empty read response (command {}, address {:#x}) "
                "for a {} byte read at {:#x} - the simulator raised on this access; its own log has the details.",
                name,
                resp_cmd,
                resp_addr,
                size,
                addr));
    }

    const uint32_t response_bytes = rd_resp_buf->data()->size() * sizeof(uint32_t);
    if (response_bytes < size) {
        nng_free(msg.data, msg.size);
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "{} read response size {} is smaller than requested size {} (address {:#x}).",
                name,
                response_bytes,
                size,
                addr));
    }
    std::memcpy(data, rd_resp_buf->data()->data(), size);
    nng_free(msg.data, msg.size);
}

void RtlSimCommunicator::write_words(AccessKind kind, tt_xy_pair core, uint64_t addr, const void *data, uint32_t size) {
    // Copied, since the caller's buffer need not be word-aligned.
    std::vector<uint32_t> words(size / sizeof(uint32_t));
    std::memcpy(words.data(), data, size);

    if (kind == AccessKind::kGlobal) {
        channel_->send(create_global_flatbuffer(DEVICE_COMMAND_GLOBAL_WRITE, words, addr));
    } else {
        const DEVICE_COMMAND command = kind == AccessKind::kSmn ? DEVICE_COMMAND_SMN_WRITE : DEVICE_COMMAND_WRITE;
        channel_->send(create_flatbuffer(command, words, core, addr));
    }
}

void RtlSimCommunicator::read_bytes(AccessKind kind, tt_xy_pair core, uint64_t addr, void *data, uint32_t size) {
    // The wire format carries whole words and some remotes drop a partial tail word when they turn
    // the bytes they read into words, so read the whole words covering the range; see word_access.hpp.
    read_bytes_as_words(addr, data, size, [&](uint64_t a, void *d, uint32_t n) { read_words(kind, core, a, d, n); });
}

void RtlSimCommunicator::write_bytes(AccessKind kind, tt_xy_pair core, uint64_t addr, const void *data, uint32_t size) {
    // The wire format carries whole words and not every remote honours the byte count, so a partial
    // first or last word is read, merged and written back whole; see word_access.hpp.
    write_bytes_as_words(
        addr,
        data,
        size,
        [&](uint64_t a, void *d, uint32_t n) { read_words(kind, core, a, d, n); },
        [&](uint64_t a, const void *d, uint32_t n) { write_words(kind, core, a, d, n); });
}

void RtlSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(channel_->request_mutex());
    log_debug(tt::LogEmulationDriver, "Device reading {} bytes from address {} in core ({}, {})", size, addr, x, y);
    read_bytes(AccessKind::kTile, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(channel_->request_mutex());
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to address {} in core ({}, {})", size, addr, x, y);
    write_bytes(AccessKind::kTile, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::global_read_words(uint64_t addr, void *data, uint32_t size) {
    validate_register_access(addr, size);
    std::lock_guard<std::mutex> request_lock(channel_->request_mutex());
    log_debug(tt::LogEmulationDriver, "Device reading {} bytes from global address {:#x}", size, addr);
    read_words(AccessKind::kGlobal, tt_xy_pair{0, 0}, addr, data, size);
}

void RtlSimCommunicator::global_write_words(uint64_t addr, const void *data, uint32_t size) {
    // The payload travels as words, so a partial word could only be dropped or written short.
    validate_register_access(addr, size);
    std::lock_guard<std::mutex> request_lock(channel_->request_mutex());
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to global address {:#x}", size, addr);
    write_words(AccessKind::kGlobal, tt_xy_pair{0, 0}, addr, data, size);
}

void RtlSimCommunicator::smn_tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(channel_->request_mutex());
    log_debug(tt::LogEmulationDriver, "Device SMN reading {} bytes from address {} in core ({}, {})", size, addr, x, y);
    read_bytes(AccessKind::kSmn, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::smn_tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(channel_->request_mutex());
    log_debug(tt::LogEmulationDriver, "Device SMN writing {} bytes to address {} in core ({}, {})", size, addr, x, y);
    write_bytes(AccessKind::kSmn, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::all_tensix_reset_assert(uint32_t x, uint32_t y) {
    log_debug(tt::LogEmulationDriver, "Sending all_tensix_reset_assert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_tensix_reset_deassert(uint32_t x, uint32_t y) {
    log_debug(tt::LogEmulationDriver, "Sending all_tensix_reset_deassert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_reset_assert(uint32_t x, uint32_t y) {
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_reset_assert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_reset_deassert(uint32_t x, uint32_t y) {
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_reset_deassert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_DEASSERT, core));
}

void RtlSimCommunicator::neo_dm_reset_assert(uint32_t x, uint32_t y, uint32_t dm_index) {
    log_debug(
        tt::LogEmulationDriver, "Sending neo_dm_reset_assert signal to core ({}, {}) for DM index {}", x, y, dm_index);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_ASSERT, {0}, core, dm_index));
}

void RtlSimCommunicator::neo_dm_reset_deassert(uint32_t x, uint32_t y, uint32_t dm_index) {
    log_debug(
        tt::LogEmulationDriver,
        "Sending neo_dm_reset_deassert signal to core ({}, {}) for DM index {}",
        x,
        y,
        dm_index);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_DEASSERT, {0}, core, dm_index));
}

void RtlSimCommunicator::all_neo_dms_uncore_reset_assert() {
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_uncore_reset_assert signal.");
    tt_xy_pair core = {0, 0};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_UNCORE_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_uncore_reset_deassert() {
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_uncore_reset_deassert signal.");
    tt_xy_pair core = {0, 0};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_UNCORE_RESET_DEASSERT, core));
}

void RtlSimCommunicator::neo_dm_uncore_reset_assert(uint32_t x, uint32_t y) {
    log_debug(tt::LogEmulationDriver, "Sending neo_dm_uncore_reset_assert signal to core ({}, {}).", x, y);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_NEO_DM_UNCORE_RESET_ASSERT, core));
}

void RtlSimCommunicator::neo_dm_uncore_reset_deassert(uint32_t x, uint32_t y) {
    log_debug(tt::LogEmulationDriver, "Sending neo_dm_uncore_reset_deassert signal to core ({}, {}).", x, y);
    tt_xy_pair core = {x, y};
    channel_->send(create_flatbuffer(DEVICE_COMMAND_NEO_DM_UNCORE_RESET_DEASSERT, core));
}

void RtlSimCommunicator::set_ram_callbacks(RamWriteCallback write_cb, RamReadCallback read_cb) {
    channel_->set_ram_callbacks(std::move(write_cb), std::move(read_cb));
}

}  // namespace tt::umd
