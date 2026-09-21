// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/protocol/quasar_protocol.hpp"

#include <fmt/format.h>

#include <cstring>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

/**
 * Every access is the same width, so a transfer is a whole number of words at a word-aligned
 * address. Mixing widths would buy nothing: the cost is the round trip, not the bytes.
 */
constexpr uint32_t ACCESS_WIDTH = 4;

}  // namespace

QuasarProtocol::QuasarProtocol(std::unique_ptr<ScalarNocAccess> access, int mmio_id) :
    access_(std::move(access)), mmio_id_(mmio_id) {
    UMD_ASSERT(access_ != nullptr, error::RuntimeError, "Quasar protocol needs a scalar access path.");
}

QuasarProtocol::~QuasarProtocol() = default;

uint32_t QuasarProtocol::access_flags(NocId noc_id) {
    switch (noc_id) {
        case NocId::NOC0:
            return 0;
        case NocId::SYSTEM_NOC:
            return ScalarNocAccess::FLAG_LOCAL_ADDRESS;
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Quasar reaches a target through one inbound aperture per address space, and {} is not one of "
                    "them.",
                    noc_to_str(noc_id)));
    }
}

void QuasarProtocol::validate(tt_xy_pair core, uint64_t addr, size_t size) {
    UMD_ASSERT(
        core.x == 0 && core.y == 0,
        error::RuntimeError,
        fmt::format(
            "A Quasar address already names its target, so it travels with no coordinate; core ({}, {}) cannot be "
            "honoured.",
            core.x,
            core.y));

    UMD_ASSERT(
        size != 0 && size % ACCESS_WIDTH == 0,
        error::RuntimeError,
        fmt::format("A {} byte transfer is not a whole number of {} byte accesses.", size, ACCESS_WIDTH));

    UMD_ASSERT(
        addr % ACCESS_WIDTH == 0,
        error::RuntimeError,
        fmt::format("Address 0x{:x} is not aligned to a {} byte access.", addr, ACCESS_WIDTH));

    UMD_ASSERT(
        size <= MAX_TRANSFER_SIZE,
        error::RuntimeError,
        fmt::format(
            "A {} byte transfer is {} driver round trips, past the {} byte limit this path serves.",
            size,
            size / ACCESS_WIDTH,
            MAX_TRANSFER_SIZE));
}

void QuasarProtocol::read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    const uint32_t flags = access_flags(noc_id);
    validate(core, addr, size);

    auto* out = static_cast<uint8_t*>(dst);
    for (size_t offset = 0; offset < size; offset += ACCESS_WIDTH) {
        uint64_t value = 0;
        access_->read(addr + offset, &value, ACCESS_WIDTH, flags);

        const auto word = static_cast<uint32_t>(value);
        std::memcpy(out + offset, &word, ACCESS_WIDTH);
    }
}

void QuasarProtocol::write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    const uint32_t flags = access_flags(noc_id);
    validate(core, addr, size);

    const auto* in = static_cast<const uint8_t*>(src);
    for (size_t offset = 0; offset < size; offset += ACCESS_WIDTH) {
        uint32_t word = 0;
        std::memcpy(&word, in + offset, ACCESS_WIDTH);

        access_->write(addr + offset, word, ACCESS_WIDTH, flags);
    }
}

// There is no bulk path to prefer: a data transfer is the same sequence of accesses as a control
// transfer, so the two differ only in what the caller means by them.
void QuasarProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    read_ctrl(dst, core, addr, size, noc_id);
}

void QuasarProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    write_ctrl(src, core, addr, size, noc_id);
}

bool QuasarProtocol::write_to_core_range(
    const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) {
    return false;
}

int QuasarProtocol::get_mmio_id() { return mmio_id_; }

}  // namespace tt::umd
