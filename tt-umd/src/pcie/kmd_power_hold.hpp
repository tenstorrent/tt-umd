// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>

#include "tt-kmd-lib/tt_kmd_lib.h"

namespace tt::umd {

/**
 * Keeps a chip powered up through tt-kmd for as long as it exists, by holding the chip's PCIe device open
 * with a power request. tt-kmd powers a chip down, clock-gating Tensix, whenever no open fd requests power;
 * clients that reach the chip without going through its PCIe device, such as JTAG, need this to keep Tensix
 * accessible.
 */
class KmdPowerHold {
public:
    /**
     * Opens the PCIe device of the chip with the given ASIC id. TT_VISIBLE_DEVICES is ignored, since tt-kmd
     * powers the chip down whether or not this process is allowed to use it over PCIe.
     * @param asic_id ASIC id as reported in the chip's firmware telemetry.
     * @return The hold, or nullptr if no PCIe device belongs to that chip.
     */
    static std::unique_ptr<KmdPowerHold> create(uint64_t asic_id);

    ~KmdPowerHold();

    KmdPowerHold(const KmdPowerHold &) = delete;
    KmdPowerHold &operator=(const KmdPowerHold &) = delete;

private:
    explicit KmdPowerHold(tt_device_t *device_handle);

    tt_device_t *device_handle_;
};

}  // namespace tt::umd
