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

// chippy word-granular ops use 32-bit accesses when given this minimum word size. This does not
// cost bulk throughput: Jtag2AxiV2Transport::read/write choose their batched series path from the
// transfer size and address alignment alone and ignore the minimum word size.
static constexpr size_t kMinWordSizeBytes = 4;

// What the catch blocks below can and cannot see.
//
// chippy reports an AXI error response out of band: the JTAG2AXI transport decodes SLVERR/DECERR,
// logs it via LOG_ERROR and returns normally. A failed access therefore arrives here as a
// successful read of zeros, and the catch only covers alignment rejections (std::invalid_argument)
// plus transport and protocol failures such as a dropped OpenOCD connection. Surfacing bus errors
// needs chippy to expose the status it already holds (AxiSingleOpDR::get_status()); until then a
// caller must validate by reading back what it wrote, never by the absence of an exception.
//
// Alignment and sub-word accesses are constrained by the same transport. With a 4-byte minimum word
// size chippy decomposes a transfer into read32/write32, which reject an address that is not 4-byte
// aligned, and it assembles a partial trailing word by zero-filling the bytes the caller did not
// supply. So an unaligned access throws where the PCIe path would succeed, and a write whose size is
// not a multiple of 4 clears the remainder of its final word. Both are open items for the Mimir
// dispatch work rather than something to paper over at this seam.

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
