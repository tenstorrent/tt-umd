// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/tt_device/hang_detection/hang_detector.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {
class PcieInterface;

/**
 * @brief HangDetector for a Blackhole reached over PCIe.
 *
 * The bus is probed through the PCIe tile's own NOC node id register over BAR0, which never leaves the tile. A
 * NOC is probed by reading the ARC tile's node id register over it, through a window of its own: the check runs
 * while an I/O through the device protocol is stalled holding that protocol's window, so it cannot share it.
 *
 * The PCIe interface is non-owning and must outlive this object.
 */
class BlackholeHangDetector : public HangDetector {
public:
    /**
     * @param probe_window Window used only for NOC probes.
     * @param arc_core_noc0 ARC core coordinate to reach over NOC0.
     * @param arc_core_noc1 ARC core coordinate to reach over NOC1.
     */
    BlackholeHangDetector(
        PcieInterface* pcie_interface,
        std::unique_ptr<TlbWindow> probe_window,
        tt_xy_pair arc_core_noc0,
        tt_xy_pair arc_core_noc1);

protected:
    uint32_t read_hang_check_reg_via_bar() override;
    uint32_t read_hang_check_reg_via_noc(NocId noc) override;
    bool is_bus_available() override;
    bool is_noc_available() override;

private:
    PcieInterface* pcie_interface_ = nullptr;
    // Threads timing out on different NOCs probe concurrently.
    std::mutex probe_window_mutex_;
    std::unique_ptr<TlbWindow> probe_window_;
    tt_xy_pair arc_core_noc0_;
    tt_xy_pair arc_core_noc1_;
};

}  // namespace tt::umd
