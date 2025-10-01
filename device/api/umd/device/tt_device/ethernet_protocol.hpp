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
    EthernetProtocol(
        std::unique_ptr<RemoteCommunication> remote_communication,
        eth_coord_t target_chip,
        architecture_implementation& architecture_implementation);

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    virtual void write_to_arc(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;
    virtual void read_from_arc(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void wait_for_non_mmio_flush() override;

    bool is_remote() override;

    void set_noc_translation_enabled(bool noc_translation_enabled) override {
        noc_translation_enabled_ = noc_translation_enabled;
    };

    eth_coord_t target_chip_{};

    // Ethernet specific methods.
    RemoteCommunication* get_remote_communication() { return remote_communication_.get(); }

private:
    std::unique_ptr<RemoteCommunication> remote_communication_ = nullptr;
    architecture_implementation& architecture_implementation_;

    bool noc_translation_enabled_ = false;
};

}  // namespace tt::umd
