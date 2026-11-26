// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/peer_window.hpp"

#include <fcntl.h>
#include <fmt/format.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "arch/tlb_window_size.hpp"
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// IoOrdering is handed to the KMD as its tt_noc_ordering value, so the encodings must match.
static_assert(static_cast<uint8_t>(IoOrdering::RELAXED) == TT_NOC_ORDERING_RELAXED);
static_assert(static_cast<uint8_t>(IoOrdering::STRICT) == TT_NOC_ORDERING_STRICT);
static_assert(static_cast<uint8_t>(IoOrdering::POSTED) == TT_NOC_ORDERING_POSTED);

struct DeviceCloser {
    void operator()(tt_device_t *dev) const { tt_device_close(dev); }
};

using DeviceHandle = std::unique_ptr<tt_device_t, DeviceCloser>;

// The window must be freed on the same handle that allocated it, so the deleter carries that handle.
struct WindowFreer {
    tt_device_t *dev;

    void operator()(tt_tlb_t *window) const { tt_tlb_free(dev, window); }
};

using WindowHandle = std::unique_ptr<tt_tlb_t, WindowFreer>;

void check_pcie_blackhole(const TTDevice &device, const char *role) {
    UMD_ASSERT(
        device.get_arch() == tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        fmt::format("Peer windows are supported only on Blackhole, but the {} device is {}.", role, device.get_arch()));
    UMD_ASSERT(
        !device.is_remote() && device.get_communication_device_type() == IODeviceType::PCIe,
        error::RuntimeError,
        fmt::format("Peer windows need devices accessed over PCIe, but the {} device is not.", role));
}

// A handle of its own rather than the TTDevice's, so the window and mapping live exactly as long as this
// object. Power aware (O_APPEND) and never voting, so it leaves the device's power state to its users.
DeviceHandle open_device(int device_id) {
    const std::string path = fmt::format("/dev/tenstorrent/{}", device_id);
    tt_device_t *dev = nullptr;
    const int ret = tt_device_open(path.c_str(), &dev, O_APPEND);
    UMD_ASSERT(ret == 0, error::RuntimeError, fmt::format("Failed to open {}: {}", path, strerror(-ret)));
    return DeviceHandle(dev);
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

PeerWindow::PeerWindow(
    TTDevice &initiator,
    TTDevice &target,
    CoreCoord core,
    uint64_t addr,
    size_t size,
    IoOrdering ordering,
    NocId noc_id) {
    check_pcie_blackhole(initiator, "initiator");
    check_pcie_blackhole(target, "target");
    const int initiator_id = initiator.get_communication_device_id();
    const int target_id = target.get_communication_device_id();
    UMD_ASSERT(
        initiator_id != target_id,
        error::RuntimeError,
        fmt::format("A peer window needs two different devices, but both are device {}.", target_id));

    const uint64_t page_size = static_cast<uint64_t>(getpagesize());
    UMD_ASSERT(size != 0, error::RuntimeError, "Cannot map a peer window of size 0.");
    UMD_ASSERT(
        addr % page_size == 0,
        error::RuntimeError,
        fmt::format("Address {:#x} must be aligned to the host page size ({} bytes).", addr, page_size));
    UMD_ASSERT(
        size % page_size == 0,
        error::RuntimeError,
        fmt::format("Size {} must be a multiple of the host page size ({} bytes).", size, page_size));

    const tt::ARCH arch = target.get_arch();
    const size_t window_size = fit_tlb_window_size(arch, addr, size);
    const uint64_t window_offset = addr % window_size;
    const tt_xy_pair noc_core = target.get_soc_descriptor().translate_chip_coord_to_translated(core, noc_id);

    // Declared so that unwinding closes the initiator handle, dropping its mapping, before the window is freed.
    DeviceHandle target_handle = open_device(target_id);
    tt_tlb_t *raw_window = nullptr;
    int ret = tt_tlb_alloc(target_handle.get(), window_size, TT_MMIO_CACHE_MODE_UC, &raw_window);
    UMD_ASSERT(
        ret == 0,
        error::RuntimeError,
        fmt::format(
            "Failed to allocate a {} byte TLB window on device {}: {}", window_size, target_id, strerror(-ret)));
    WindowHandle window(raw_window, WindowFreer{target_handle.get()});

    tt_noc_addr_config_t noc_config{};
    noc_config.addr = addr - window_offset;
    noc_config.x_end = static_cast<uint16_t>(noc_core.x);
    noc_config.y_end = static_cast<uint16_t>(noc_core.y);
    noc_config.noc = static_cast<uint8_t>(noc_id);
    noc_config.ordering = static_cast<uint8_t>(ordering);
    noc_config.static_vc = static_cast<uint8_t>(get_architecture_tlbs(arch).use_static_vc);
    ret = tt_tlb_map(target_handle.get(), window.get(), &noc_config);
    UMD_ASSERT(
        ret == 0,
        error::RuntimeError,
        fmt::format(
            "Failed to aim a TLB window of device {} at {:#x} on core {}: {}",
            target_id,
            noc_config.addr,
            noc_core.str(),
            strerror(-ret)));

    uint32_t window_id = 0;
    tt_tlb_get_id(window.get(), &window_id);
    const BarRange window_range = window_bar_range(arch, window_id);
    // Only the requested range is mapped, so the initiator cannot reach the rest of the window.
    const uint64_t bar_offset = window_range.offset + window_offset;
    // The KMD takes a 32-bit BAR offset and length.
    UMD_ASSERT(
        bar_offset <= std::numeric_limits<uint32_t>::max() && size <= std::numeric_limits<uint32_t>::max(),
        error::RuntimeError,
        fmt::format(
            "{} bytes at offset {:#x} of BAR{} of device {} are beyond what the kernel driver can map for a peer.",
            size,
            bar_offset,
            window_range.bar,
            target_id));

    DeviceHandle initiator_handle = open_device(initiator_id);
    ret = tt_device_map_peer_bar(
        initiator_handle.get(),
        target_handle.get(),
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
        "Device {} reaches {} bytes at {:#x} on core {} of device {} at {:#x} through TLB window {}.",
        initiator_id,
        size,
        addr,
        noc_core.str(),
        target_id,
        address_,
        window_id);

    initiator_handle_ = initiator_handle.release();
    window_ = window.release();
    target_handle_ = target_handle.release();
}

PeerWindow::~PeerWindow() noexcept {
    // Closing the initiator's handle is the only way to remove its mapping, and it must be gone before the
    // window can be handed to anyone else.
    tt_device_close(initiator_handle_);
    tt_tlb_free(target_handle_, window_);
    tt_device_close(target_handle_);
}

uint64_t PeerWindow::get_address() const { return address_; }

}  // namespace tt::umd
