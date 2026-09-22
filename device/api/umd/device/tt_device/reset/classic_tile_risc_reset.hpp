// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device/reset/risc_reset.hpp"

namespace tt::umd {
class ArchitectureImplementation;
class DeviceProtocol;

/**
 * @brief RiscReset for architectures with one soft-reset register per tile: each operation is a
 * read-modify-write of that register over the device protocol, with the register address and the
 * RiscType-to-bits mapping served by the ArchitectureImplementation.
 *
 * The interfaces are non-owning and must outlive this object.
 */
class ClassicTileRiscReset : public RiscReset {
public:
    ClassicTileRiscReset(DeviceProtocol* device_protocol, ArchitectureImplementation* architecture_impl);

    void assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id = NocId::DEFAULT_NOC) override;

    void deassert_risc_reset(
        tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id = NocId::DEFAULT_NOC) override;

    std::optional<RiscType> get_risc_reset_state(tt_xy_pair core, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    uint32_t read_reset_register(tt_xy_pair core, NocId noc_id);
    void write_reset_register(tt_xy_pair core, uint32_t value, NocId noc_id);

    DeviceProtocol* device_protocol_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;
};

}  // namespace tt::umd
