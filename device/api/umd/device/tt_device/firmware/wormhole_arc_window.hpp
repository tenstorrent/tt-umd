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

/**
 * @brief Access to one of the Wormhole ARC address windows.
 *
 * The ARC tile exposes several windows that differ only in where they sit and whether they hold
 * registers or memory; how an access reaches them is the same for all of them. This holds that
 * common routing and takes the differences as a Config, so a window is added by naming its
 * constants rather than by copying the class.
 *
 * A single access is routed one of three ways, in order: over the NOC when the device is reached
 * remotely, over JTAG when it is reached that way, and otherwise through the PCIe BAR. Callers pass
 * the ARC core coordinate and the NOC to route over, so this holds no NOC state of its own.
 *
 * Taking both per call is a deliberate difference from the WormholeTTDevice APB accessors this
 * replaced, which pinned their JTAG branch to wormhole::ARC_CORES_NOC0[0] and NocId::DEFAULT_NOC
 * whatever NOC the thread had selected, and only used a NOC-dependent core on the remote branch.
 * Every route here uses what the caller passed, so a caller that wants the old JTAG behaviour has
 * to pass the NOC0 coordinate itself. Routing is otherwise unchanged.
 *
 * The interfaces are non-owning and must outlive this object. This is the only copy of the APB
 * routing; the firmware component and the SPI device reach it directly.
 */
class WormholeArcWindow {
public:
    /**
     * @brief What a window holds, which is what the remote route has to match.
     *
     * This only selects the remote route's access kind. The JTAG route issues a register access for
     * every window, memory ones included, which is what WormholeTTDevice::read_from_arc_csm did: it
     * is reading a single word out of the ARC's address space over a debug transport, where the
     * register path is the one that is wired up, so the distinction does not arise.
     */
    enum class Content : uint8_t {
        /** Registers, reached over the protocol's register path. */
        REGISTERS,
        /** Memory, reached over the protocol's data path when remote. */
        MEMORY,
    };

    /**
     * @brief Where one window sits and what it holds.
     */
    struct Config {
        const char* name;
        uint64_t noc_base_address;
        uint32_t bar0_offset_start;
        uint32_t size_bytes;
        Content content;
    };

    /**
     * @brief Builds access to the ARC APB register window over one communication protocol.
     * @param device_protocol Protocol to issue NOC accesses through. Required.
     * @param pcie_interface BAR access, when the device is reached over PCIe.
     * @param jtag_interface JTAG access, when the device is reached over JTAG.
     * @param remote_interface Ethernet access, when the device is reached through a gateway.
     *
     * Exactly one of the three transport interfaces must be given: which one is present is how the
     * route is picked, and a TTDevice is built for exactly one communication protocol.
     */
    static WormholeArcWindow arc_apb(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        RemoteInterface* remote_interface);

    /**
     * @brief Builds access to the ARC CSM memory window over one communication protocol.
     * @param device_protocol Protocol to issue NOC accesses through. Required.
     * @param pcie_interface BAR access, when the device is reached over PCIe.
     * @param jtag_interface JTAG access, when the device is reached over JTAG.
     * @param remote_interface Ethernet access, when the device is reached through a gateway.
     *
     * Same transport rule as arc_apb(). CSM holds memory rather than registers, so a remote access
     * takes the data path; see Content for why JTAG does not.
     *
     * Blackhole has no counterpart: BlackholeTTDevice's CSM accessors only ever threw, so there
     * was nothing to extract there.
     */
    static WormholeArcWindow arc_csm(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        RemoteInterface* remote_interface);

    /**
     * @brief Reads from the window.
     * @param mem_ptr Destination buffer.
     * @param arc_addr_offset Offset into the window. The whole transfer has to fit inside it, so the
     * largest valid offset is the window size less size.
     * @param size Bytes to read. The remote path honors any size; the JTAG and BAR paths read one
     * word and reject anything other than sizeof(uint32_t) rather than silently widening.
     * @param arc_core NOC coordinate of the ARC core, resolved for noc_id. Used on every route,
     * including JTAG, which WormholeTTDevice pinned to NOC0 instead.
     * @param noc_id NOC to route through.
     */
    void read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id);

    /**
     * @brief Writes to the window.
     * @param mem_ptr Source buffer.
     * @param arc_addr_offset Offset into the window. The whole transfer has to fit inside it, so the
     * largest valid offset is the window size less size.
     * @param size Bytes to write. The remote path honors any size; the JTAG and BAR paths write one
     * word and reject anything other than sizeof(uint32_t) rather than silently widening.
     * @param arc_core NOC coordinate of the ARC core, resolved for noc_id. Used on every route,
     * including JTAG, which WormholeTTDevice pinned to NOC0 instead.
     * @param noc_id NOC to route through.
     */
    void write(const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id);

private:
    WormholeArcWindow(
        const Config& config,
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        RemoteInterface* remote_interface);

    void check_access(uint64_t arc_addr_offset, size_t size) const;

    Config config_;
    // All non-owning; they belong to the component that owns this object and must outlive it.
    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    JtagInterface* jtag_interface_ = nullptr;
    RemoteInterface* remote_interface_ = nullptr;
};

}  // namespace tt::umd
