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
class RemoteInterface;
class architecture_implementation;

/**
 * @brief Access to the Wormhole ARC CSM memory window.
 *
 * Same three routes as WormholeArcApb (remote over the NOC, JTAG, then the PCIe BAR), but the remote
 * path is a data access rather than a register access, and the offsets are the CSM ones. Callers pass
 * the ARC core coordinate and the NOC to route over, so this holds no NOC state of its own.
 *
 * The interfaces are non-owning and must outlive this object.
 */
class WormholeArcCsm {
public:
    WormholeArcCsm(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        RemoteInterface* remote_interface,
        architecture_implementation* architecture_impl);

    /**
     * @brief Reads from the ARC CSM window.
     * @param mem_ptr Destination buffer.
     * @param arc_addr_offset Offset into the ARC CSM window.
     * @param size Bytes to read; only honored on the remote path, the other paths read one word.
     * @param arc_core NOC coordinate of the ARC core, resolved for noc_id.
     * @param noc_id NOC to route through.
     */
    void read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id);

    /**
     * @brief Writes to the ARC CSM window.
     * @param mem_ptr Source buffer.
     * @param arc_addr_offset Offset into the ARC CSM window.
     * @param size Bytes to write; only honored on the remote path, the other paths write one word.
     * @param arc_core NOC coordinate of the ARC core, resolved for noc_id.
     * @param noc_id NOC to route through.
     */
    void write(const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id);

private:
    // All non-owning; they belong to the component that owns this object and must outlive it.
    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    JtagInterface* jtag_interface_ = nullptr;
    RemoteInterface* remote_interface_ = nullptr;
    architecture_implementation* architecture_impl_ = nullptr;
};

}  // namespace tt::umd
