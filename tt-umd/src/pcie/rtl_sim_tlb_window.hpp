// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/types/io_window_config.hpp"

namespace tt::umd {

class RtlSimCommunicator;
class SocDescriptor;
class TlbHandle;

namespace att {
class EndpointResolver;
}

/**
 * IoWindow over a simulated TLB that translates TLB-based memory access
 * into read/write calls on RtlSimCommunicator.
 * Since RTL sim has no PCIe BAR0, the TLB config (core coordinates + address)
 * is used to reconstruct the target core and address for each access.
 *
 * It honors WindowFlags::Snoop: on a device that sends coordinates, Snoop requested through
 * configure(TargetIoWindowConfig) is translated into AXI awuser/aruser bit 8 (cce_cmd_snoop)
 * and forwarded to the communicator on every access.
 */
class RtlSimTlbWindow : public IoWindow {
public:
    /**
     * @param resolver When set, the device uses flat addresses: every access is resolved through
     *        it exactly like the device's own host accesses, so a window never sends a raw
     *        (x, y, addr) the simulator would interpret in another coordinate frame. Null for a
     *        device that sends coordinates.
     * @param soc_descriptor The device's descriptor, used with @p resolver.
     */
    RtlSimTlbWindow(
        std::unique_ptr<TlbHandle> handle,
        RtlSimCommunicator* communicator,
        const att::EndpointResolver* resolver,
        const SocDescriptor* soc_descriptor);

    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write64(uint64_t offset, uint64_t value) override;
    uint64_t read64(uint64_t offset) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;
    void write_aligned(uint64_t offset, const void* data, size_t size) override;
    void read_aligned(uint64_t offset, void* data, size_t size) override;
    void configure(const TargetIoWindowConfig& config) override;
    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override;
    TargetIoWindowConfig get_target_config() const override;
    IoOrdering get_io_ordering() const override;
    size_t get_size() const override;
    HostMemoryCaching get_memory_caching_type() const override;

private:
    // The simulator honors AXI user bits only on tile reads and writes, not on the global accesses a
    // flat-address device makes, so snoop is expressible only when the window sends coordinates.
    WindowFlags supported_window_flags() const { return resolver_ == nullptr ? WindowFlags::Snoop : WindowFlags::None; }

    /**
     * Translate a TLB window offset to (core, address) and perform a write via the communicator.
     */
    void translate_and_write(uint64_t offset, const void* data, size_t size);

    /**
     * Translate a TLB window offset to (core, address) and perform a read via the communicator.
     */
    void translate_and_read(uint64_t offset, void* data, size_t size);

    std::unique_ptr<TlbHandle> tlb_handle_;
    // Distance from the TLB-size aligned address the entry maps to the window base.
    uint64_t offset_from_aligned_addr_ = 0;
    // Non-direction WindowFlags of the current mapping; the TLB entry has no field for them.
    WindowFlags window_flags_ = WindowFlags::None;
    RtlSimCommunicator* communicator_;
    const att::EndpointResolver* resolver_;
    const SocDescriptor* soc_descriptor_;
};

}  // namespace tt::umd
