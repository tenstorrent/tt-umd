// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "tt-umd/tt_device/reset/risc_reset.hpp"

namespace tt::umd {
class DeviceProtocol;

/**
 * Where a tile's TSC (tile system controller) keeps its resets. TSC_RESET_CTRL is an 8-byte register
 * of active-low fields, 1 = released. The overlay's soft resets are ANDed with the TSC's and come up
 * held, so releasing DM cores releases them too.
 */
struct TscResetLayout {
    uint64_t reset_ctrl;
    // core_reset_n: DM core i at dm_core_lsb + i.
    uint32_t dm_core_lsb;
    uint32_t num_dm_cores;
    // uncore_reset_n, the DM cores' shared uncore.
    uint32_t dm_uncore_bit;
    // tensix_risc_reset_n: TRISC i at risc_lsb + i.
    uint32_t risc_lsb;
    uint32_t num_riscs;
    uint64_t overlay_core_soft_reset;
    // Every core the widest tile has; a core whose TSC bit is clear stays held regardless.
    uint32_t overlay_core_soft_reset_mask;
    uint64_t overlay_uncore_soft_reset;
};

/** Horizon's Tensix and Dispatch tiles, from aether's tsc_full_map_reg and tensix_neo_reg. */
inline constexpr TscResetLayout HORIZON_TSC_RESET{
    .reset_ctrl = 0x0303'3088,
    .dm_core_lsb = 8,
    .num_dm_cores = 8,
    .dm_uncore_bit = 24,
    .risc_lsb = 36,
    .num_riscs = 4,
    .overlay_core_soft_reset = 0x0300'0328,
    .overlay_core_soft_reset_mask = 0xFFF,
    .overlay_uncore_soft_reset = 0x0300'032C,
};

/**
 * @brief RiscReset for tiles whose RISCs are held by their TSC: each operation is a read-modify-write
 * of the tile's TSC_RESET_CTRL over the device protocol, which reaches it like any other tile
 * register. RiscType::ALL and the ALL_* groups cover the cores the layout has; DM cores map to
 * core_reset_n, NEO0's TRISCs to tensix_risc_reset_n and the DM uncore flags to uncore_reset_n.
 *
 * The protocol is non-owning and must outlive this object.
 */
class TscRiscReset : public RiscReset {
public:
    TscRiscReset(DeviceProtocol* device_protocol, const TscResetLayout& layout);

    void assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id = NocId::DEFAULT_NOC) override;

    void deassert_risc_reset(
        tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id = NocId::DEFAULT_NOC) override;

    std::optional<RiscType> get_risc_reset_state(tt_xy_pair core, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    // The TSC_RESET_CTRL bits of @p riscs.
    uint64_t reset_bits(RiscType riscs) const;
    uint64_t read_reset_ctrl(tt_xy_pair core, NocId noc_id);
    void write_reset_ctrl(tt_xy_pair core, uint64_t value, NocId noc_id);
    void write_overlay(tt_xy_pair core, uint64_t addr, uint32_t value, NocId noc_id);

    DeviceProtocol* device_protocol_ = nullptr;
    TscResetLayout layout_;
    // Serializes each read-modify-write so concurrent resets of different RISCs on one tile keep each other's bits.
    std::mutex reset_register_mutex_;
};

}  // namespace tt::umd
