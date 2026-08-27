// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Chip specific API examples.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

// TODO: Once default auto TLB setup is in, check it is setup properly.
TEST(ApiChipTest, DISABLED_ManualTLBConfiguration) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();

    // Expect to throw for remote chip for any worker core.
    auto remote_chips = umd_cluster->get_target_remote_device_ids();
    if (!remote_chips.empty()) {
        ChipId any_remote_chip = *remote_chips.begin();
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(any_remote_chip);
        CoreCoord core = soc_desc.get_cores(CoreType::TENSIX)[0];
        EXPECT_THROW(umd_cluster->get_static_tlb_window(any_remote_chip, core), std::runtime_error);
    }

    // Expect to throw for non configured mmio chip.
    ChipId any_mmio_chip = *umd_cluster->get_target_mmio_device_ids().begin();
    const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(any_mmio_chip);
    CoreCoord core = soc_desc.get_cores(CoreType::TENSIX)[0];
    EXPECT_THROW(umd_cluster->get_static_tlb_window(any_mmio_chip, core), std::runtime_error);

    std::int32_t c_zero_address = 0;

    // Each MMIO chip has it's own set of TLBs, so needs its own configuration.
    for (ChipId mmio_chip : umd_cluster->get_target_mmio_device_ids()) {
        any_mmio_chip = mmio_chip;
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(mmio_chip);
        for (CoreCoord core : soc_desc.get_cores(CoreType::TENSIX)) {
            umd_cluster->configure_tlb(mmio_chip, core, 1 << 20, c_zero_address);
        }
    }

    // Expect not to throw for now configured mmio chip, same one as before.
    EXPECT_NO_THROW(umd_cluster->get_static_tlb_window(any_mmio_chip, core));

    // Expect to throw for non worker cores.
    CoreCoord dram_core = soc_desc.get_dram_cores()[0][0];
    EXPECT_THROW(umd_cluster->get_static_tlb_window(any_mmio_chip, dram_core), std::runtime_error);
    auto eth_cores = soc_desc.get_cores(CoreType::ETH);
    if (!eth_cores.empty()) {
        CoreCoord eth_core = eth_cores[0];
        EXPECT_THROW(umd_cluster->get_static_tlb_window(any_mmio_chip, eth_core), std::runtime_error);
    }
}

// TODO: Move to test_chip.
TEST(ApiChipTest, SimpleAPIShowcase) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();

    ChipId chip_id = umd_cluster->get_cluster_description()->get_chips_with_mmio().begin()->first;

    // TODO: In future, will be accessed through Chip api.
    umd_cluster->get_pcie_base_addr_from_device(chip_id);
    umd_cluster->get_num_host_channels(chip_id);
}
