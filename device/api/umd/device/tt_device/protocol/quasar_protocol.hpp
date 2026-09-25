// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/kmd_scalar_noc_access.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Device I/O for Quasar, carried by the kernel driver's scalar accesses.
 *
 * Quasar exposes no translation window for userspace to map, so this protocol cannot be built on
 * TlbWindow the way PcieProtocol is. Every transfer is decomposed into single naturally aligned
 * accesses, each of which is one round trip through the driver.
 *
 * The address is flat and carries its own target, so the coordinate the interface takes is not a
 * destination here and only the origin is accepted. The NOC selects the inbound aperture instead:
 * the default one reaches ordinary targets, and the system NOC reaches the chiplet-local space
 * where the translation and firewall configuration blocks live.
 */
class QuasarProtocol : public DeviceProtocol {
public:
    /**
     * An access is a driver round trip, so a large transfer is a correspondingly large amount of
     * time rather than a large amount of bandwidth. Refusing past this point keeps a caller that
     * meant to use a bulk path from waiting on hundreds of thousands of them instead.
     */
    static constexpr size_t MAX_TRANSFER_SIZE = 4096;

    QuasarProtocol(std::shared_ptr<KmdScalarNocAccess> access, int mmio_id);
    ~QuasarProtocol() override;

    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;

    /** Always false: there is no hardware multicast on this path, so the caller must unicast. */
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;

    int get_mmio_id() override;

private:
    /** Aperture flags for @p noc_id, rejecting a NOC this path cannot serve. */
    static uint32_t access_flags(NocId noc_id);

    /** Rejects a request the driver would refuse, naming what is wrong with it. */
    static void validate(tt_xy_pair core, uint64_t addr, size_t size);

    std::shared_ptr<KmdScalarNocAccess> access_;
    int mmio_id_;
};

}  // namespace tt::umd
