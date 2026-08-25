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
    /**
     * @brief Builds ARC APB access over one communication protocol.
     * @param device_protocol Protocol to issue NOC accesses through. Required.
     * @param pcie_interface BAR access, when the device is reached over PCIe.
     * @param jtag_interface JTAG access, when the device is reached over JTAG.
     *
     * Exactly one of pcie_interface and jtag_interface must be given: which one is present is how
     * the route is picked, and a TTDevice is built for exactly one communication protocol.
     */
    BlackholeArcApb(DeviceProtocol* device_protocol, PcieInterface* pcie_interface, JtagInterface* jtag_interface);

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
};

}  // namespace tt::umd
