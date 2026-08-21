// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class DeviceProtocol;
class JtagInterface;
class PcieInterface;
class architecture_implementation;

/**
 * @brief Access to the Blackhole ARC APB register window.
 *
 * A single access is routed one of three ways, in order: over JTAG when the device is reached that
 * way, over the NOC when the ARC tile is not reachable over AXI, and otherwise through the PCIe BAR.
 * Callers pass the ARC core coordinate and the NOC to route over, so this holds no NOC state of its
 * own.
 *
 * The interfaces are non-owning and must outlive this object.
 */
class BlackholeArcApb {
public:
    BlackholeArcApb(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        architecture_implementation* architecture_impl);

    /**
     * @brief Reads from the ARC APB window.
     * @param mem_ptr Destination buffer.
     * @param arc_addr_offset Offset into the ARC APB window.
     * @param size Bytes to read; only honored on the NOC path, the other paths read one word.
     * @param arc_core NOC coordinate of the ARC core, resolved for noc_id.
     * @param noc_id NOC to route through.
     */
    void read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id);

    /**
     * @brief Writes to the ARC APB window.
     * @param mem_ptr Source buffer.
     * @param arc_addr_offset Offset into the ARC APB window.
     * @param size Bytes to write; only honored on the NOC path, the other paths write one word.
     * @param arc_core NOC coordinate of the ARC core, resolved for noc_id.
     * @param noc_id NOC to route through.
     */
    void write(const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id);

private:
    bool is_arc_available_over_axi();

    int get_pcie_x_coordinate();

    // All non-owning; they belong to the component that owns this object and must outlive it.
    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    JtagInterface* jtag_interface_ = nullptr;
    architecture_implementation* architecture_impl_ = nullptr;
};

}  // namespace tt::umd
