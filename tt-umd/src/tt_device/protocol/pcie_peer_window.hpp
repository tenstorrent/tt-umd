/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tt-umd/tt_device/protocol/peer_dma_interface.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/tlb.hpp"

struct tt_device_t;
struct tt_tlb_t;

namespace tt::umd {

/**
 * PeerWindow over a TLB window of one PCIe device, mapped for another PCIe device by the kernel driver.
 *
 * Holds kernel driver handles of its own on both devices rather than sharing the TTDevices' handles: the driver
 * frees a window when the handle that allocated it closes, but drops a peer mapping only when the initiator's
 * handle closes, so only handles this object controls let it remove the mapping before the window is freed.
 */
class PciePeerWindow : public PeerWindow {
public:
    // Aims a window_size byte window of device target_id at config and maps [window_offset, window_offset + size)
    // of it for device initiator_id. window_size must be a size class lying within the first 4 GiB of its BAR,
    // since the driver takes 32-bit BAR offsets for a peer mapping.
    PciePeerWindow(
        tt::ARCH arch,
        int target_id,
        int initiator_id,
        size_t window_size,
        const tlb_data &config,
        uint64_t window_offset,
        size_t size);

    ~PciePeerWindow() override;

    uint64_t get_address() const override;

private:
    struct DeviceCloser {
        void operator()(tt_device_t *dev) const;
    };

    // The window must be freed through the handle that allocated it, so the deleter carries that handle.
    struct WindowFreer {
        tt_device_t *dev;

        void operator()(tt_tlb_t *window) const;
    };

    using DeviceHandle = std::unique_ptr<tt_device_t, DeviceCloser>;
    using WindowHandle = std::unique_ptr<tt_tlb_t, WindowFreer>;

    int target_id_;
    int initiator_id_;
    // Members are destroyed in reverse order, also when the constructor throws: the initiator handle closes,
    // dropping its mapping, before the window is freed, and the window is freed before its own handle closes.
    DeviceHandle target_handle_;
    WindowHandle window_;
    DeviceHandle initiator_handle_;
    uint64_t address_ = 0;
};

}  // namespace tt::umd
