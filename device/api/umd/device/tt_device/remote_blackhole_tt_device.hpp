/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip/local_chip.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"

namespace tt::umd {

class RemoteBlackholeTTDevice : public BlackholeTTDevice {
public:
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void noc_multicast_write(
        void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void wait_for_non_mmio_flush() override;

    RemoteCommunication* get_remote_communication();

protected:
    bool is_arc_available_over_axi() override;

private:
    RemoteBlackholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication, bool allow_spi = false);

    friend std::unique_ptr<TTDevice> TTDevice::create(
        std::unique_ptr<RemoteCommunication> remote_communication, bool allow_spi);

    std::unique_ptr<RemoteCommunication> remote_communication_;
};

}  // namespace tt::umd
