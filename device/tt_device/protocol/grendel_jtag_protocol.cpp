/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/protocol/grendel_jtag_protocol.hpp"

#include <fmt/format.h>

#include <exception>

#include "grendel_jtag_protocol_impl.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// chippy word-granular ops use 32-bit accesses when given this minimum word size.
static constexpr size_t kMinWordSizeBytes = 4;

GrendelJtagProtocol::GrendelJtagProtocol(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

GrendelJtagProtocol::~GrendelJtagProtocol() = default;

void GrendelJtagProtocol::write_to_device(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    if (noc_id != NocId::NOC0) {
        UMD_THROW(
            error::RuntimeError, fmt::format("GrendelJtagProtocol supports only NOC0 (got {}).", noc_to_str(noc_id)));
    }
    try {
        // chippy's TransportInterface::write() takes a non-const void* even though it only reads the
        // buffer, so the const has to be cast away at the boundary.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
        impl_->transport_for(core)->write(size, kMinWordSizeBytes, addr, const_cast<void*>(mem_ptr));
    } catch (const std::exception& e) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "GrendelJtagProtocol write to core ({}, {}) addr 0x{:x} failed: {}", core.x, core.y, addr, e.what()));
    }
}

void GrendelJtagProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    if (noc_id != NocId::NOC0) {
        UMD_THROW(
            error::RuntimeError, fmt::format("GrendelJtagProtocol supports only NOC0 (got {}).", noc_to_str(noc_id)));
    }
    try {
        impl_->transport_for(core)->read(size, kMinWordSizeBytes, addr, mem_ptr);
    } catch (const std::exception& e) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "GrendelJtagProtocol read from core ({}, {}) addr 0x{:x} failed: {}", core.x, core.y, addr, e.what()));
    }
}

bool GrendelJtagProtocol::write_to_core_range(
    const void* /*mem_ptr*/,
    tt_xy_pair /*core_start*/,
    tt_xy_pair /*core_end*/,
    uint64_t /*addr*/,
    uint32_t /*size*/,
    NocId /*noc_id*/) {
    // Hardware multicast is deferred; the caller falls back to software unicast.
    return false;
}

int GrendelJtagProtocol::get_mmio_id() { return impl_->mmio_id; }

JtagDevice* GrendelJtagProtocol::get_jtag_device() { return nullptr; }

}  // namespace tt::umd
