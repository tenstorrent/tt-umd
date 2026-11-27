/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tt_device/protocol/pcie_peer_window.hpp"

#include <fcntl.h>
#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <tt-logger/tt-logger.hpp>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// tlb_data carries the ordering in its hardware encoding, which the driver takes as is.
static_assert(tlb_data::Relaxed == TT_NOC_ORDERING_RELAXED);
static_assert(tlb_data::Strict == TT_NOC_ORDERING_STRICT);
static_assert(tlb_data::Posted == TT_NOC_ORDERING_POSTED);

// Power aware (O_APPEND) and never voting, so the window leaves each device's power state to its users.
tt_device_t *open_device(int device_id) {
    const std::string path = fmt::format("/dev/tenstorrent/{}", device_id);
    tt_device_t *dev = nullptr;
    const int ret = tt_device_open(path.c_str(), &dev, O_APPEND);
    UMD_ASSERT(ret == 0, error::RuntimeError, fmt::format("Failed to open {}: {}", path, strerror(-ret)));
    return dev;
}

// Where a window sits in the device's BARs: Blackhole puts its 4 GiB windows in BAR4, the rest in BAR0.
struct BarRange {
    uint32_t bar;
    uint64_t offset;
};

BarRange window_bar_range(tt::ARCH arch, uint32_t window_id) {
    for (const TlbSizeClass &size_class : get_architecture_tlbs(arch).size_classes) {
        if (window_id >= size_class.base_index && window_id - size_class.base_index < size_class.count) {
            return BarRange{
                size_class.in_bar4 ? 4U : 0U,
                size_class.bar_offset + (window_id - size_class.base_index) * size_class.size};
        }
    }
    UMD_THROW(error::RuntimeError, fmt::format("TLB window {} is not a window {} provides.", window_id, arch));
}

}  // namespace

void PciePeerWindow::DeviceCloser::operator()(tt_device_t *dev) const { tt_device_close(dev); }

void PciePeerWindow::WindowFreer::operator()(tt_tlb_t *window) const { tt_tlb_free(dev, window); }

PciePeerWindow::PciePeerWindow(
    tt::ARCH arch,
    int target_id,
    int initiator_id,
    size_t window_size,
    const tlb_data &config,
    uint64_t window_offset,
    size_t size) :
    target_id_(target_id), initiator_id_(initiator_id) {
    // Both devices are opened before any window is taken, so a missing initiator costs nothing to unwind.
    target_handle_.reset(open_device(target_id));
    initiator_handle_.reset(open_device(initiator_id));

    tt_tlb_t *raw_window = nullptr;
    int ret = tt_tlb_alloc(target_handle_.get(), window_size, TT_MMIO_CACHE_MODE_UC, &raw_window);
    UMD_ASSERT(
        ret == 0,
        error::RuntimeError,
        fmt::format(
            "Failed to allocate a {} byte TLB window on device {}: {}", window_size, target_id, strerror(-ret)));
    window_ = WindowHandle(raw_window, WindowFreer{target_handle_.get()});

    tt_noc_addr_config_t noc_config{};
    noc_config.addr = config.local_offset;
    noc_config.x_end = static_cast<uint16_t>(config.x_end);
    noc_config.y_end = static_cast<uint16_t>(config.y_end);
    noc_config.noc = static_cast<uint8_t>(config.noc_sel);
    noc_config.ordering = static_cast<uint8_t>(config.ordering);
    noc_config.static_vc = static_cast<uint8_t>(config.static_vc);
    ret = tt_tlb_map(target_handle_.get(), window_.get(), &noc_config);
    UMD_ASSERT(
        ret == 0,
        error::RuntimeError,
        fmt::format(
            "Failed to aim a TLB window of device {} at {:#x} on core ({}, {}): {}",
            target_id,
            config.local_offset,
            config.x_end,
            config.y_end,
            strerror(-ret)));

    uint32_t window_id = 0;
    tt_tlb_get_id(window_.get(), &window_id);
    const BarRange window_range = window_bar_range(arch, window_id);
    // Only the requested range is mapped, so the initiator cannot reach the rest of the window.
    const uint64_t bar_offset = window_range.offset + window_offset;
    UMD_ASSERT(
        bar_offset + size <= std::numeric_limits<uint32_t>::max(),
        error::RuntimeError,
        fmt::format(
            "{} bytes at offset {:#x} of BAR{} of device {} are beyond what the kernel driver can map for a peer.",
            size,
            bar_offset,
            window_range.bar,
            target_id));

    ret = tt_device_map_peer_bar(
        initiator_handle_.get(),
        target_handle_.get(),
        window_range.bar,
        static_cast<uint32_t>(bar_offset),
        static_cast<uint32_t>(size),
        &address_);
    UMD_ASSERT(
        ret == 0,
        error::RuntimeError,
        fmt::format(
            "Failed to map {} bytes at {:#x} of BAR{} of device {} for device {}: {}",
            size,
            bar_offset,
            window_range.bar,
            target_id,
            initiator_id,
            strerror(-ret)));

    log_debug(
        LogUMD,
        "Device {} reaches {} bytes at {:#x} on core ({}, {}) of device {} at {:#x} through TLB window {}.",
        initiator_id,
        size,
        config.local_offset + window_offset,
        config.x_end,
        config.y_end,
        target_id,
        address_,
        window_id);
}

PciePeerWindow::~PciePeerWindow() {
    log_debug(
        LogUMD,
        "Releasing the window of device {} mapped for device {} at {:#x}.",
        target_id_,
        initiator_id_,
        address_);
}

uint64_t PciePeerWindow::get_address() const { return address_; }

}  // namespace tt::umd
