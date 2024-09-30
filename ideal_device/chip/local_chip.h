/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "chip/chip.h"
#include "tt_device/tt_device.h"

namespace tt::umd {

enum class tlb_type {
    tlb_1m,
    tlb_2m,
    tlb_16m,
    tlb_4gb,
};

struct tlb_index {
    tlb_type type;
    int index;
}

class LocalChip: public Chip {
public:
    // This chip has to be a local chip. If not, it fails.
    LocalChip(uint32_t device_id): Chip(device_id) {}

    // Local chip has associated TTDevice
    std::unique_ptr<TTDevice> tt_device;

    // Not available for remote chip.
    virtual void configure_active_ethernet_cores_for_mmio_device(
        const std::unordered_set<physical_coord>& active_eth_cores_per_chip);
    
    // Also not available for remote chip.
    // TLB setup is done by default, and is hidden behind chip implementation.
    // If you want to have your own TLB setup, you have to grab the TTDevice and do it there.
    // After that you setup the core to tlb mapping here.
    // This all has to be done before you start using the chip (start_device), or it will fail.
    virtual void setup_core_to_tlb_map(std::unordered_map<tlb_index,  mapping_function);
    
};

}