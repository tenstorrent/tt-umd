// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>

#include "umd/device/tt_device/reset/risc_reset.hpp"

namespace tt::umd {
class ArchitectureImplementation;
class TTDevice;

/**
 * @brief RiscReset for a TTSim device: a read-modify-write of the soft-reset register, inverted and
 * widened to 64 bits on Quasar, whose DM cores hold the opposite reset polarity.
 *
 * The accesses go through the device rather than its protocol because only the device dispatches
 * between a simulator in this process and one reached over a socket. The device and the
 * architecture implementation are owned by the backend and must outlive this object.
 */
class TTSimRiscReset : public RiscReset {
public:
    TTSimRiscReset(
        TTDevice* device,
        ArchitectureImplementation* architecture_impl,
        bool inverted_reset_polarity,
        std::recursive_mutex& device_lock);

    void assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id = NocId::DEFAULT_NOC) override;

    void deassert_risc_reset(
        tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id = NocId::DEFAULT_NOC) override;

    std::optional<RiscType> get_risc_reset_state(tt_xy_pair core, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    // Applies bits to the register, either setting or clearing them.
    void update_reset_register(tt_xy_pair core, RiscType selected_riscs, bool set_bits);

    TTDevice* device_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;
    bool inverted_reset_polarity_ = false;
    std::recursive_mutex& device_lock_;
};

}  // namespace tt::umd
