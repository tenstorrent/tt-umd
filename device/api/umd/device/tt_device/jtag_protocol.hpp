/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/jtag/jtag_device.hpp"

namespace tt::umd {

class JtagProtocol : public DeviceProtocol {
public:
    JtagProtocol(JtagDevice* jtag_device, uint8_t jlink_id, architecture_implementation& architecture_implementation) :
        jtag_device_(jtag_device), jlink_id_(jlink_id), architecture_implementation_(architecture_implementation){};

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    virtual void write_to_arc(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;
    virtual void read_from_arc(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void wait_for_non_mmio_flush() override;
    bool is_remote() override;

    void set_noc_translation_enabled(bool noc_translation_enabled) override {
        noc_translation_enabled_ = noc_translation_enabled;
    };

private:
    JtagDevice* jtag_device_;
    uint8_t jlink_id_;
    architecture_implementation& architecture_implementation_;

    bool noc_translation_enabled_ = false;
};

}  // namespace tt::umd
