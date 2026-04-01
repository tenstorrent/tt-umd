// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

/**
 * @brief TLB manager for simulation environments.
 * 
 * This class provides TLB management for simulation chips where no real PCIe
 * hardware exists. It maps IOVA addresses directly to virtual addresses.
 */
class SimulationTlbManager : public TLBManager {
public:
    /**
     * @brief Construct a new Simulation TLB Manager.
     * 
     * @param arch The architecture of the simulated chip
     */
    explicit SimulationTlbManager(tt::ARCH arch);
    ~SimulationTlbManager() = default;

    // Delete copy/move semantics
    SimulationTlbManager(const SimulationTlbManager&) = delete;
    SimulationTlbManager& operator=(const SimulationTlbManager&) = delete;
    SimulationTlbManager(SimulationTlbManager&&) = delete;
    SimulationTlbManager& operator=(SimulationTlbManager&&) = delete;

private:
    tt::ARCH arch_;
};

}  // namespace tt::umd
