// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <fstream>
#include <cassert>
#include <random>
#include <gtest/gtest.h>

#include "tt_device.h"
#include "l1_address_map.h"
#include "new_device/soc_descriptor.h"
#include "tests/test_utils/generate_cluster_desc.hpp"

class uBenchmarkFixture : public ::testing::Test {
    protected:
    void SetUp() override {
        // get arch name?
        results_csv.open("ubench_results.csv", std::ios_base::app);

        auto get_static_tlb_index = [] (xy_pair target) {
            int flat_index = target.y * 10 + target.x;  // grid_size_x = 10 for GS/WH ????? something is wrong here
            if (flat_index == 0) {
                return -1;
            }
            return flat_index;
        };
        std::set<chip_id_t> target_devices = {0};
        std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {{"SMALL_READ_WRITE_TLB", 157}}; // Use both static and dynamic TLBs here
        uint32_t num_host_mem_ch_per_mmio_device = 1;
        device = std::make_shared<LocalChip>(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config, false, true);

        for(int i = 0; i < target_devices.size(); i++) {
            // Iterate over devices and only setup static TLBs for functional worker cores
            auto& sdesc = device->get_virtual_soc_descriptors().at(i);
            for(auto& core : sdesc.workers) {
                // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE. 
                device->configure_tlb(i, core, get_static_tlb_index(core), l1_mem::address_map::DATA_BUFFER_SPACE_BASE);
            }
        }
    }

    void TearDown() override {
        device->close_device();
        results_csv.close();
    }

    std::shared_ptr<LocalChip> device;
    std::ofstream results_csv;
};
