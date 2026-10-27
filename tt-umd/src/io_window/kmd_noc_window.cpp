// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "io_window/kmd_noc_window.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

#include "common/noc_address_space.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr uint32_t WORD_WIDTH = 4;

/**
 * The address space a target names, refusing one this path cannot represent.
 *
 * A coordinate and a grid are both refused rather than ignored: every transfer is issued at
 * config_.addr alone, so honouring them silently would send the access somewhere else.
 */
NocAddressSpace accept_target(const TargetIoWindowConfig& config) {
    UMD_ASSERT(
        config.core_start.x == 0 && config.core_start.y == 0,
        error::RuntimeError,
        fmt::format(
            "A flat address already names its target, so a window travels with no coordinate; core ({}, {}) cannot "
            "be honoured.",
            config.core_start.x,
            config.core_start.y));

    UMD_ASSERT(
        !config.core_end.has_value(),
        error::RuntimeError,
        "There is no multicast on this path, so a window cannot cover a grid of cores.");

    // The direction field is advice: WindowFlags leaves an implementation that gains nothing from it
    // free to ignore the field, and the reconfiguring helpers stamp UnicastRead or UnicastWrite on
    // every transfer they make. Atomic and Snoop are not advice -- they ask for a transaction the
    // driver's scalar access has no way to mark -- so they are refused rather than quietly dropped.
    const WindowFlags unsupported = config.flags & ~WindowFlags::DirectionMask;
    UMD_ASSERT(
        unsupported == WindowFlags::None,
        error::RuntimeError,
        fmt::format(
            "A scalar access carries no transaction attributes, so flags 0x{:x} cannot be honoured.",
            static_cast<std::underlying_type_t<WindowFlags>>(unsupported)));

    return noc_address_space(config.noc.value_or(NocId::NOC0));
}

}  // namespace

KmdNocWindow::KmdNocWindow(PCIDevice* pci_device, const TargetIoWindowConfig& config, size_t size) :
    pci_device_(pci_device), config_(config), space_(accept_target(config)), size_(size) {
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "An I/O window needs an open device.");
}

KmdNocWindow::~KmdNocWindow() = default;

void KmdNocWindow::validate(uint64_t offset, size_t size, uint32_t width) const {
    UMD_ASSERT(
        offset % width == 0,
        error::RuntimeError,
        fmt::format("Offset 0x{:x} is not aligned to a {} byte access.", offset, width));
    UMD_ASSERT(
        size != 0 && size % width == 0,
        error::RuntimeError,
        fmt::format("A {} byte transfer is not a whole number of {} byte accesses.", size, width));

    // A window with a size accepts offsets within it. One created with no size has nothing
    // allocated to overrun, so the target's own translation is what bounds it.
    UMD_ASSERT(
        size_ == 0 || (offset <= size_ && size <= size_ - offset),
        error::RuntimeError,
        fmt::format("A {} byte access at offset 0x{:x} runs past this {} byte window.", size, offset, size_));
}

void KmdNocWindow::write32(uint64_t offset, uint32_t value) {
    validate(offset, WORD_WIDTH, WORD_WIDTH);
    pci_device_->noc_write_scalar(config_.addr + offset, value, WORD_WIDTH, space_);
}

uint32_t KmdNocWindow::read32(uint64_t offset) {
    validate(offset, WORD_WIDTH, WORD_WIDTH);
    return static_cast<uint32_t>(pci_device_->noc_read_scalar(config_.addr + offset, WORD_WIDTH, space_));
}

// The driver serves a 64-bit access natively, so this is one round trip rather than two.
void KmdNocWindow::write64(uint64_t offset, uint64_t value) {
    validate(offset, sizeof(uint64_t), sizeof(uint64_t));
    pci_device_->noc_write_scalar(config_.addr + offset, value, sizeof(uint64_t), space_);
}

uint64_t KmdNocWindow::read64(uint64_t offset) {
    validate(offset, sizeof(uint64_t), sizeof(uint64_t));
    return pci_device_->noc_read_scalar(config_.addr + offset, sizeof(uint64_t), space_);
}

void KmdNocWindow::write16(uint64_t offset, uint16_t value) {
    validate(offset, sizeof(uint16_t), sizeof(uint16_t));
    pci_device_->noc_write_scalar(config_.addr + offset, value, sizeof(uint16_t), space_);
}

uint16_t KmdNocWindow::read16(uint64_t offset) {
    validate(offset, sizeof(uint16_t), sizeof(uint16_t));
    return static_cast<uint16_t>(pci_device_->noc_read_scalar(config_.addr + offset, sizeof(uint16_t), space_));
}

void KmdNocWindow::write_block(uint64_t offset, const void* data, size_t size) {
    validate(offset, size, WORD_WIDTH);

    const auto* in = static_cast<const uint8_t*>(data);
    for (size_t done = 0; done < size; done += WORD_WIDTH) {
        uint32_t word = 0;
        std::memcpy(&word, in + done, WORD_WIDTH);
        pci_device_->noc_write_scalar(config_.addr + offset + done, word, WORD_WIDTH, space_);
    }
}

void KmdNocWindow::read_block(uint64_t offset, void* data, size_t size) {
    validate(offset, size, WORD_WIDTH);

    auto* out = static_cast<uint8_t*>(data);
    for (size_t done = 0; done < size; done += WORD_WIDTH) {
        const auto word =
            static_cast<uint32_t>(pci_device_->noc_read_scalar(config_.addr + offset + done, WORD_WIDTH, space_));
        std::memcpy(out + done, &word, WORD_WIDTH);
    }
}

// Every access is a separate driver round trip already, so there is no looser bulk path for
// read_block to take and no stricter register path for these to take.
void KmdNocWindow::write_aligned(uint64_t offset, const void* data, size_t size) { write_block(offset, data, size); }

void KmdNocWindow::read_aligned(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void KmdNocWindow::configure(const TargetIoWindowConfig& config) {
    space_ = accept_target(config);
    config_ = config;
}

// The ordering argument is accepted and ignored: the driver performs one access at a time and
// returns when it is done, so there is no weaker ordering available to select.
void KmdNocWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) { configure(config); }

TargetIoWindowConfig KmdNocWindow::get_target_config() const { return config_; }

IoOrdering KmdNocWindow::get_io_ordering() const { return IoOrdering::Strict; }

// What this window accepts offsets within: the caller's requested size, or, when the caller left the
// choice here, everything still addressable from the current base. Valid offsets are [0, get_size()),
// so reporting 0 for the unbounded case would claim there are none, and a caller sizing its chunks
// off this -- transfer_and_reconfigure does -- would never advance.
size_t KmdNocWindow::get_size() const {
    if (size_ != 0) {
        return size_;
    }

    const uint64_t addressable = std::numeric_limits<uint64_t>::max() - config_.addr;
    return static_cast<size_t>(std::min<uint64_t>(addressable, std::numeric_limits<size_t>::max()));
}

HostMemoryCaching KmdNocWindow::get_memory_caching_type() const { return HostMemoryCaching::UC; }

}  // namespace tt::umd
