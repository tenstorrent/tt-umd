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
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * GrendelJtagProtocol implements DeviceProtocol (and JtagInterface) for a Grendel
 * package reached over JTAG, backed by the chippy grendel access stack.
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
 */
class GrendelJtagProtocol : public DeviceProtocol, public JtagInterface {
public:
    ~GrendelJtagProtocol() override;

    // DeviceProtocol interface.
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    bool write_to_core_range(
        const void* mem_ptr,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        uint32_t size,
        NocId noc_id) override;
    int get_mmio_id() override;

    // JtagInterface. Grendel has no UMD JtagDevice (that is the JLink-specific
    // WH/BH path), so this returns nullptr.
    JtagDevice* get_jtag_device() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit GrendelJtagProtocol(std::unique_ptr<Impl> impl);

    friend struct GrendelJtagProtocolTestAccess;
};

}  // namespace tt::umd
