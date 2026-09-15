/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * GrendelJtagProtocol implements DeviceProtocol for a Grendel package reached over JTAG, backed by
 * the chippy grendel access stack.
 *
 * It deliberately does not implement JtagInterface: that interface exposes a memory-mapped 32-bit
 * register window, which is how the Wormhole/Blackhole ARC paths reach a JLink-connected device.
 * Grendel access is routed per core through chippy, and the ARC paths it serves do not apply to
 * Mimir bring-up.
 *
 * It is a router: each device access is dispatched, by the target core, to the
 * owning chiplet's chippy transport. chippy is a C++20 dependency, so it is kept
 * entirely behind a pimpl (`Impl`, defined in the internal, chippy-aware header
 * grendel_jtag_protocol_impl.hpp) — no chippy type appears in this public header,
 * and UMD's public API stays C++17.
 *
 * Construction is not public here: the production entry point (create()) is added
 * alongside the chippy connection factory; tests construct via
 * GrendelJtagProtocolTestAccess with an injected transport provider.
 *
 * Access constraints inherited from the chippy transport, which differ from the PCIe path:
 * addresses must be 4-byte aligned, a write whose size is not a multiple of 4 zero-fills the rest
 * of its final word, and an AXI error response is not raised as an exception -- chippy logs it and
 * returns, so a failed read yields zeros. Validate by read-back, not by absence of a throw.
 */
class GrendelJtagProtocol : public DeviceProtocol {
public:
    ~GrendelJtagProtocol() override;

    // DeviceProtocol interface. chippy exposes one transport per chiplet with no separate data and
    // control paths, so the ctrl variants add the register-access validation and then share the
    // data path.
    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;
    int get_mmio_id() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit GrendelJtagProtocol(std::unique_ptr<Impl> impl);

    friend struct GrendelJtagProtocolTestAccess;
};

}  // namespace tt::umd
