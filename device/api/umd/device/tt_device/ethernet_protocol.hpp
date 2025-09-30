/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "device_protocol.hpp"
#include "remote_communication.hpp"

namespace tt::umd {

class EthernetProtocol : public DeviceProtocol {
public:
    EthernetProtocol(std::unique_ptr<RemoteCommunication> remote_communication, eth_coord_t target_chip);

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void wait_for_non_mmio_flush() override;

    bool is_remote() override;

    eth_coord_t target_chip_{};

private:
    std::unique_ptr<RemoteCommunication> remote_communication_ = nullptr;
};

}  // namespace tt::umd
