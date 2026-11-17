// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/tt_device/hang_detection/hang_detector.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

class DeviceProtocol;
class PcieInterface;

/**
 * @brief Transport plumbing shared by the per-architecture hang detectors.
 *
 * Decides which checks apply from the interfaces it was given: the bus check needs BAR access, the
 * NOC check needs a protocol that reaches the NOC. The per-architecture subclass names the register
 * each check reads.
 *
 * The interfaces are non-owning and must outlive this object.
 */
class HangDetectorImplementation : public HangDetector {
public:
    /**
     * @brief Gives NOC probes a window of their own.
     *
     * A NOC probe runs while another I/O on the device may be stuck holding the protocol's own
     * windows, so it must not need them. Until a window is given, probes go through the protocol.
     */
    void set_noc_probe_window(std::unique_ptr<IoWindow> window);

protected:
    /**
     * @param device_protocol Protocol for NOC probes; nullptr disables the NOC check.
     * @param pcie_interface BAR access for the bus check; nullptr disables the bus check.
     */
    HangDetectorImplementation(DeviceProtocol* device_protocol, PcieInterface* pcie_interface);

    bool is_bus_available() override;
    bool is_noc_available() override;

    // Reads one 32-bit register at (core, addr) over noc, through the probe window when one is set.
    uint32_t read_noc_register(tt_xy_pair core, uint64_t addr, NocId noc);

    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;

private:
    std::mutex noc_probe_window_mutex_;
    std::unique_ptr<IoWindow> noc_probe_window_;
};

}  // namespace tt::umd
