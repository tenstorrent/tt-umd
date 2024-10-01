/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/*

Questions:

- Do we need per arch TLB manager (for example for UC/WC distribution)?

- Do we need to have index in TLBWindow in order to return it properly?

*/

#include "pci_device.h"
#include "tlb_window.h"

namespace tt::umd {

class TLBManager {

public:
    virtual TLBWindow get_tlb_window(tt_xy_pair core) = 0;
    
    virtual TLBWindow get_wc_tlb_window() = 0;

    virtual TLBWindow get_uc_tlb_window() = 0;

    virtual void release_tlb_window(TLBWindow& tlb_window) = 0;

    // This is something that is probably going to need per arch implementation.
    virtual TLBWindow get_dram_tlb_widow(uint32_t dram_channel, uint32_t addr = 0) = 0;

    virtual TLBWindow release_dram_tlb_window(TLBWindow& tlb_window) = 0;
protected:
    // Are these constants supposed to be in per arch tlb manager class?
    static constexpr uint32_t uc_window_start = 0;
    static constexpr uint32_t uc_window_count = 0;

    static constexpr uint32_t wc_window_start = 0;
    static constexpr uint32_t wc_window_count = 0;

    // dummy implementation
    uint32_t map_core_to_tlb(tt_xy_pair core) {
        return 0;
    }
}

// This should represent how we do things today. We have whole BAR0/4 space
// and just map the TLB we need.
class StandardTLBManager : public TLBManager {

public:
    StandardTLBManager(PCIDevice* pci_device) : pci_device(pci_device) {}

    TLBWindow get_tlb_window(tt_xy_pair core) override {
        uint32_t tlb_index = map_core_to_tlb_index(core);
        // This should be a WC window.
        if (tlb_index < wc_window_start || tlb_index >= uc_window_start) {
            // throw error.
        }
        wc_tlb_window_count[tlb_index - wc_window_start]++;

        return get_tlb_window(tlb_index);
    }
    
    // Get unused WC TLB window.
    TLBWindow get_wc_tlb_window() override {
        // Find unused UC TLB window.
        for (uint32_t i = 0; i < wc_window_count; i++) {
            if (wc_tlb_window_count[i] == 0) {
                wc_tlb_window_count[i]++;
                return get_tlb_window(wc_window_start + i);
            }
        }

        // throw error.
    }

    // Get unused UC TLB window.
    TLBWindow get_uc_tlb_window() override {
        // Find unused UC TLB window.
        for (uint32_t i = 0; i < uc_window_count; i++) {
            if (uc_tlb_window_count[i] == 0) {
                uc_tlb_window_count[i]++;
                return get_tlb_window(uc_window_start + i);
            }
        }
    }

    void release_tlb_window(TLBWindow& tlb_window) override {
        uint32_t tlb_index = tlb_window.index;
        if (tlb_index >= wc_window_start && tlb_index < uc_window_start) {
            // Do we need to reprogram it?
            wc_tlb_window_count[tlb_index - wc_window_start]--;
        } else if (tlb_index >= uc_window_start) {
            // Do we need to reprogram it?
            uc_tlb_window_count[tlb_index - uc_window_start]--;
        } else {
            // throw error.
        }
    }

    TLBWindow get_dram_tlb_widow(uint32_t dram_channel, uint32_t addr = 0) override {
        // We need to get DRAM core from DRAM channel.
        return get_wc_tlb_window(tt_xy_pair(0, 0));
    }

    void release_dram_tlb_window(TLBWindow& tlb_window) override {
        release_tlb_window(tlb_window);
    }

private:
    PCIDevice* pci_device;

    // Do we allow reuse of WC TLB windows?
    uint32_t wc_tlb_window_count[wc_window_count];
    uint32_t uc_tlb_window_count[uc_window_count];


    TLBWindow get_tlb_window(uint32_t tlb_index) {
        if (!dev->pci_device->bar0_wc) {
            throw std::runtime_error("No write-combined mapping for BAR0");
        }

        auto tlb_data = pci_device->describe_tlb(tlb_index);

        if (!tlb_data.has_value()) {
            throw std::runtime_error("No TLB mapped to core " + target.str());
        }

        auto [tlb_offset, tlb_size] = tlb_data.value();
        uint8_t* base = reinterpret_cast<uint8_t*>(pci_device->bar0_wc);

        return TLBWindow(base + tlb_offset, tlb_size);
    }
}

// This sould represent how we would like to do resource management in the future.
// Every call to get a TLBWindow should go to KMD if possible.

/*

- How should KMD calls look like?

- Do we want to buffer TLB windows for cores?

- We probably want to buffer TLB windows for DRAM regions.

*/

class KMDTLBManager : public TLBManager {

public:
    TLBWindow get_tlb_window(tt_xy_pair core) override {
        // kmd call.
    }
    
    TLBWindow get_wc_tlb_window() override {
        // kmd call.
    }

    TLBWindow get_uc_tlb_window() override {
        // kmd call.
    }

    void release_tlb_window(TLBWindow& tlb_window) override {
        // kmd call.
    }

    TLBWindow get_dram_tlb_widow(uint32_t dram_channel, uint32_t addr = 0) override {
        // kmd call.
    }

    void release_dram_tlb_window(TLBWindow& tlb_window) override {
        // kmd call.
    }
}

}