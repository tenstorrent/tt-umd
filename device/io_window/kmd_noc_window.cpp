// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/io_window/kmd_noc_window.hpp"

#include <fmt/format.h>

#include <cstring>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr uint32_t WORD_WIDTH = 4;

void check_aligned(uint64_t offset, size_t size, uint32_t width) {
    UMD_ASSERT(
        offset % width == 0,
        error::RuntimeError,
        fmt::format("Offset 0x{:x} is not aligned to a {} byte access.", offset, width));
    UMD_ASSERT(
        size != 0 && size % width == 0,
        error::RuntimeError,
        fmt::format("A {} byte transfer is not a whole number of {} byte accesses.", size, width));
}

}  // namespace

KmdNocWindow::KmdNocWindow(std::unique_ptr<KmdScalarNocAccess> access, const TargetIoWindowConfig& config) :
    access_(std::move(access)), config_(config) {
    UMD_ASSERT(access_ != nullptr, error::RuntimeError, "An I/O window needs a scalar access path.");
}

KmdNocWindow::~KmdNocWindow() = default;

uint32_t KmdNocWindow::access_flags() const {
    return config_.noc.value_or(NocId::NOC0) == NocId::SYSTEM_NOC ? KmdScalarNocAccess::FLAG_LOCAL_ADDRESS : 0;
}

void KmdNocWindow::write32(uint64_t offset, uint32_t value) {
    check_aligned(offset, WORD_WIDTH, WORD_WIDTH);
    access_->write(config_.addr + offset, value, WORD_WIDTH, access_flags());
}

uint32_t KmdNocWindow::read32(uint64_t offset) {
    check_aligned(offset, WORD_WIDTH, WORD_WIDTH);

    uint64_t value = 0;
    access_->read(config_.addr + offset, &value, WORD_WIDTH, access_flags());
    return static_cast<uint32_t>(value);
}

void KmdNocWindow::write16(uint64_t offset, uint16_t value) {
    check_aligned(offset, sizeof(uint16_t), sizeof(uint16_t));
    access_->write(config_.addr + offset, value, sizeof(uint16_t), access_flags());
}

uint16_t KmdNocWindow::read16(uint64_t offset) {
    check_aligned(offset, sizeof(uint16_t), sizeof(uint16_t));

    uint64_t value = 0;
    access_->read(config_.addr + offset, &value, sizeof(uint16_t), access_flags());
    return static_cast<uint16_t>(value);
}

void KmdNocWindow::write_block(uint64_t offset, const void* data, size_t size) {
    check_aligned(offset, size, WORD_WIDTH);

    const auto* in = static_cast<const uint8_t*>(data);
    for (size_t done = 0; done < size; done += WORD_WIDTH) {
        uint32_t word = 0;
        std::memcpy(&word, in + done, WORD_WIDTH);
        access_->write(config_.addr + offset + done, word, WORD_WIDTH, access_flags());
    }
}

void KmdNocWindow::read_block(uint64_t offset, void* data, size_t size) {
    check_aligned(offset, size, WORD_WIDTH);

    auto* out = static_cast<uint8_t*>(data);
    for (size_t done = 0; done < size; done += WORD_WIDTH) {
        uint64_t value = 0;
        access_->read(config_.addr + offset + done, &value, WORD_WIDTH, access_flags());

        const auto word = static_cast<uint32_t>(value);
        std::memcpy(out + done, &word, WORD_WIDTH);
    }
}

// Every access is a separate driver round trip already, so there is no looser bulk path for
// read_block to take and no stricter register path for these to take.
void KmdNocWindow::write_aligned(uint64_t offset, const void* data, size_t size) { write_block(offset, data, size); }

void KmdNocWindow::read_aligned(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void KmdNocWindow::configure(const TargetIoWindowConfig& config) { config_ = config; }

// The ordering argument is accepted and ignored: the driver performs one access at a time and
// returns when it is done, so there is no weaker ordering available to select.
void KmdNocWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) { configure(config); }

TargetIoWindowConfig KmdNocWindow::get_target_config() const { return config_; }

IoOrdering KmdNocWindow::get_io_ordering() const { return IoOrdering::Strict; }

// Nothing is mapped, so there is no region whose extent could be reported.
size_t KmdNocWindow::get_size() const { return 0; }

HostMemoryCaching KmdNocWindow::get_memory_caching_type() const { return HostMemoryCaching::UC; }

}  // namespace tt::umd
