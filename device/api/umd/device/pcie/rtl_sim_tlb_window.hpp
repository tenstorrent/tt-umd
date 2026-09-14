// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "umd/device/pcie/tlb_window.hpp"

namespace tt::umd {

class RtlSimulationTTDevice;
class TlbHandle;
struct tlb_data;

/**
 * RTL simulation TlbWindow implementation. Since RTL sim has no PCIe BAR0, the TLB config
 * (core coordinates + address) is used to reconstruct the target core and address for each
 * access, which then goes through RtlSimulationTTDevice::resolved_tile_write/read.
 */
class RtlSimTlbWindow : public TlbWindow {
public:
    /**
     * @param device Every access is resolved through the device (its NoC address resolver, when
     *        installed) exactly like host_write/host_read, so a TLB window never sends a raw
     *        (x, y, addr) the simulator would interpret in another coordinate frame.
     */
    RtlSimTlbWindow(std::unique_ptr<TlbHandle> handle, RtlSimulationTTDevice* device, const tlb_data config = {});

    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write_register(uint64_t offset, const void* data, size_t size) override;
    void read_register(uint64_t offset, void* data, size_t size) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;

private:
    /**
     * Translate a TLB window offset to (core, address) and perform a write through the device.
     */
    void translate_and_write(uint64_t offset, const void* data, size_t size);

    /**
     * Translate a TLB window offset to (core, address) and perform a read through the device.
     */
    void translate_and_read(uint64_t offset, void* data, size_t size);

    RtlSimulationTTDevice* device_;
};

}  // namespace tt::umd
