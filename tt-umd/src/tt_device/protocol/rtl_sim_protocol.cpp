// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/rtl_sim_protocol.hpp"

#include <utility>

#include "register_access.hpp"
#include "simulation/word_access.hpp"
#include "tt-umd/simulation/rtl_sim_session.hpp"

namespace tt::umd {

RtlSimProtocol::RtlSimProtocol(const std::filesystem::path& simulator_directory, bool flat_addresses, int mmio_id) :
    communicator_(std::make_unique<RtlSimCommunicator>(simulator_directory)),
    flat_addresses_(flat_addresses),
    mmio_id_(mmio_id) {}

RtlSimProtocol::RtlSimProtocol(const RtlSimSocket& socket, bool flat_addresses, int mmio_id) :
    communicator_(std::make_unique<RtlSimCommunicator>(socket)), flat_addresses_(flat_addresses), mmio_id_(mmio_id) {}

void RtlSimProtocol::initialize() { communicator_->initialize(); }

void RtlSimProtocol::shutdown() { communicator_->shutdown(); }

void RtlSimProtocol::set_ram_callbacks(
    RtlSimCommunicator::RamWriteCallback write_cb, RtlSimCommunicator::RamReadCallback read_cb) {
    communicator_->set_ram_callbacks(std::move(write_cb), std::move(read_cb));
}

void RtlSimProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    const auto bytes = static_cast<uint32_t>(size);
    std::lock_guard<std::mutex> lock(io_lock_);
    if (noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_read_bytes(core.x, core.y, addr, dst, bytes);
    } else if (flat_addresses_) {
        read_bytes_as_words(
            addr, dst, bytes, [this](uint64_t a, void* d, uint32_t n) { communicator_->global_read_words(a, d, n); });
    } else {
        communicator_->tile_read_bytes(core.x, core.y, addr, dst, bytes);
    }
}

void RtlSimProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    const auto bytes = static_cast<uint32_t>(size);
    std::lock_guard<std::mutex> lock(io_lock_);
    if (noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_write_bytes(core.x, core.y, addr, src, bytes);
    } else if (flat_addresses_) {
        write_bytes_as_words(
            addr,
            src,
            bytes,
            [this](uint64_t a, void* d, uint32_t n) { communicator_->global_read_words(a, d, n); },
            [this](uint64_t a, const void* d, uint32_t n) { communicator_->global_write_words(a, d, n); });
    } else {
        communicator_->tile_write_bytes(core.x, core.y, addr, src, bytes);
    }
}

void RtlSimProtocol::read_ctrl(
    void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    read_registers_individually(*this, dst, core, addr, size, noc_id, width);
}

void RtlSimProtocol::write_ctrl(
    const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    write_registers_individually(*this, src, core, addr, size, noc_id, width);
}

bool RtlSimProtocol::write_to_core_range(
    const void* /*src*/,
    tt_xy_pair /*core_start*/,
    tt_xy_pair /*core_end*/,
    uint64_t /*addr*/,
    size_t /*size*/,
    NocId /*noc_id*/) {
    return false;
}

int RtlSimProtocol::get_mmio_id() { return mmio_id_; }

void RtlSimProtocol::tile_read_bytes(tt_xy_pair core, uint64_t addr, void* dst, size_t size, uint64_t user_bits) {
    std::lock_guard<std::mutex> lock(io_lock_);
    communicator_->tile_read_bytes(core.x, core.y, addr, dst, static_cast<uint32_t>(size), user_bits);
}

void RtlSimProtocol::tile_write_bytes(
    tt_xy_pair core, uint64_t addr, const void* src, size_t size, uint64_t user_bits) {
    std::lock_guard<std::mutex> lock(io_lock_);
    communicator_->tile_write_bytes(core.x, core.y, addr, src, static_cast<uint32_t>(size), user_bits);
}

void RtlSimProtocol::all_tensix_reset_assert(tt_xy_pair core) {
    communicator_->all_tensix_reset_assert(core.x, core.y);
}

void RtlSimProtocol::all_tensix_reset_deassert(tt_xy_pair core) {
    communicator_->all_tensix_reset_deassert(core.x, core.y);
}

void RtlSimProtocol::all_neo_dms_reset_assert(tt_xy_pair core) {
    communicator_->all_neo_dms_reset_assert(core.x, core.y);
}

void RtlSimProtocol::all_neo_dms_reset_deassert(tt_xy_pair core) {
    communicator_->all_neo_dms_reset_deassert(core.x, core.y);
}

void RtlSimProtocol::neo_dm_reset_assert(tt_xy_pair core, uint32_t dm_index) {
    communicator_->neo_dm_reset_assert(core.x, core.y, dm_index);
}

void RtlSimProtocol::neo_dm_reset_deassert(tt_xy_pair core, uint32_t dm_index) {
    communicator_->neo_dm_reset_deassert(core.x, core.y, dm_index);
}

void RtlSimProtocol::all_neo_dms_uncore_reset_assert() { communicator_->all_neo_dms_uncore_reset_assert(); }

void RtlSimProtocol::all_neo_dms_uncore_reset_deassert() { communicator_->all_neo_dms_uncore_reset_deassert(); }

void RtlSimProtocol::neo_dm_uncore_reset_assert(tt_xy_pair core) {
    communicator_->neo_dm_uncore_reset_assert(core.x, core.y);
}

void RtlSimProtocol::neo_dm_uncore_reset_deassert(tt_xy_pair core) {
    communicator_->neo_dm_uncore_reset_deassert(core.x, core.y);
}

}  // namespace tt::umd
