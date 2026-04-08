// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/chip/local_chip.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"

namespace tt::umd {

class RemoteWormholeTTDevice : public WormholeTTDevice {
public:
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void noc_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

    void wait_for_non_mmio_flush() override;

    void dma_write_to_device(const void* src, size_t size, tt_xy_pair core, uint64_t addr) override;

    void dma_read_from_device(void* dst, size_t size, tt_xy_pair core, uint64_t addr) override;

    void dma_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

private:
    RemoteWormholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication);

    friend std::unique_ptr<TTDevice> TTDevice::create(std::unique_ptr<RemoteCommunication> remote_communication);
};

}  // namespace tt::umd
