/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "device_communication.hpp"
#include "remote_communication.hpp"

namespace tt::umd {

class EthernetCommunication : TTDeviceCommunication {
public:
    EthernetCommunication(std::unique_ptr<RemoteCommunication> remote_communication, eth_coord_t target_chip) :
        remote_communication(std::move(remote_communication)), target_chip(target_chip){};

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr) override;
    void read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr) override;

    void write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) override;
    void write_regs(uint32_t byte_addr, uint32_t word_len, const void* data) override;
    void read_regs(uint32_t byte_addr, uint32_t word_len, void* data) override;

    void wait_for_non_mmio_flush() override;

    bool is_remote() override { return true; };

    RemoteCommunication* get_remote_communication();

private:
    std::unique_ptr<RemoteCommunication> remote_communication;
    eth_coord_t target_chip;
};

}  // namespace tt::umd
