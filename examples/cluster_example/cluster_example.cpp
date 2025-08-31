// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <memory>

#include "umd/device/cluster.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    std::cout << "=== Tensix Soft Reset Example ===" << std::endl;

    try {
        // Create a cluster instance for the first available device
        std::unique_ptr<Cluster> cluster = Cluster::create();

        if (!cluster) {
            std::cerr << "Failed to create cluster - no devices found?" << std::endl;
            return 1;
        }

        // Get the first chip ID and a Tensix core coordinate
        auto chip_ids = cluster->get_all_chips();
        if (chip_ids.empty()) {
            std::cerr << "No chips found in cluster" << std::endl;
            return 1;
        }

        chip_id_t chip = chip_ids[0];
        CoreCoord tensix_core(1, 1, CoreType::TENSIX, CoordSystem::LOGICAL);  // First Tensix core

        std::cout << "Using chip " << chip << ", core " << tensix_core.x << "," << tensix_core.y << std::endl;

        // Example 1: Check current soft reset state
        std::cout << "\n--- Checking current soft reset state ---" << std::endl;
        RiscType current_state = cluster->get_soft_reset_state(chip, tensix_core);
        std::cout << "Current reset state: " << RiscTypeToString(current_state) << std::endl;

        // Example 2: Assert reset for specific RISC cores
        std::cout << "\n--- Asserting reset for BRISC and TRISC0 ---" << std::endl;
        RiscType cores_to_reset = RiscType::BRISC | RiscType::TRISC0;
        std::cout << "Asserting reset for: " << RiscTypeToString(cores_to_reset) << std::endl;
        cluster->assert_risc_reset(chip, tensix_core, cores_to_reset);

        // Check state after assert
        RiscType state_after_assert = cluster->get_soft_reset_state(chip, tensix_core);
        std::cout << "State after assert: " << RiscTypeToString(state_after_assert) << std::endl;

        // Example 3: Deassert reset with staggered start
        // Note that this code might crash if there is no program set for the core to run.
        std::cout << "\n--- Deasserting reset with staggered start ---" << std::endl;
        cluster->deassert_risc_reset(chip, tensix_core, cores_to_reset, true);

        // Check final state
        RiscType final_state = cluster->get_soft_reset_state(chip, tensix_core);
        std::cout << "Final state after deassert: " << RiscTypeToString(final_state) << std::endl;

        // Example 4: Architecture-agnostic usage
        std::cout << "\n--- Using architecture-agnostic flags ---" << std::endl;
        std::cout << "Asserting all TRISCs: " << RiscTypeToString(RiscType::ALL_TRISCS) << std::endl;
        cluster->assert_risc_reset(chip, tensix_core, RiscType::ALL_TRISCS);

        RiscType trisc_state = cluster->get_soft_reset_state(chip, tensix_core);
        std::cout << "State with all TRISCs reset: " << RiscTypeToString(trisc_state) << std::endl;

        // Deassert without staggered start
        std::cout << "Deasserting TRISCs without staggered start" << std::endl;
        cluster->deassert_risc_reset(chip, tensix_core, RiscType::ALL_TRISCS, false);

        std::cout << "\n=== Tensix Soft Reset Example Complete ===" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
