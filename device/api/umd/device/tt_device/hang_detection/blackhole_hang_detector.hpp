/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

#include "umd/device/tt_device/hang_detection/hang_detector_implementation.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class DeviceProtocol;
class architecture_implementation;
enum class NocId : uint8_t;

// Blackhole variant: reads BAR and NOC node ID from the PCIe tile.
// NOC1 hang detection requires knowing whether NOC translation is enabled,
// because the PCIe tile y-coordinate differs (0 vs 11).
class BlackholeHangDetector : public HangDetectorImplementation {
public:
    BlackholeHangDetector(
        DeviceProtocol* protocol, architecture_implementation* arch_impl, bool noc_translation_enabled);

private:
    uint32_t read_hang_check_reg_via_bar() override;
    uint32_t read_hang_check_reg_via_noc(NocId noc) override;

    tt_xy_pair get_hang_check_core(NocId noc) const;

    bool noc_translation_enabled_;
};

}  // namespace tt::umd
