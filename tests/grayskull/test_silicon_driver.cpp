// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <thread>

#include "gtest/gtest.h"
#include "tt_device.h"
#include "device/tt_soc_descriptor.h"
#include "device/wormhole/wormhole_implementation.h"
#include "l1_address_map.h"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "tests/test_utils/device_test_utils.hpp"

TEST(SiliconDriverGS, CreateDestroySequential) {
    std::set<chip_id_t> target_devices = {0};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_device_params default_params;
    for(int i = 0; i < 100; i++) {
        tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true);
        device.start_device(default_params);
        device.deassert_risc_reset();
        device.close_device();
    }
}

TEST(SiliconDriverGS, CreateMultipleInstance) {
    std::set<chip_id_t> target_devices = {0};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_device_params default_params;
    default_params.init_device = false;
    std::unordered_map<int, tt_SiliconDevice*> concurrent_devices = {};
    for(int i = 0; i < 100; i++) {
        concurrent_devices.insert({i, new tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true)});
        concurrent_devices.at(i) -> start_device(default_params);
    }

    for(auto& device : concurrent_devices) {
        device.second -> close_device();
        delete device.second;
    }
}

TEST(SiliconDriverGS, Harvesting) {
    std::set<chip_id_t> target_devices = {0};
    std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {{0, 6}, {1, 12}};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true, true, simulated_harvesting_masks);
    auto sdesc_per_chip = device.get_virtual_soc_descriptors();

    ASSERT_EQ(device.using_harvested_soc_descriptors(), true) << "Expected Driver to have performed harvesting";
    for(const auto& chip : sdesc_per_chip) {
        ASSERT_LE(chip.second.workers.size(), 96) << "Expected SOC descriptor with harvesting to have less than or equal to 96 workers for chip " << chip.first;
    }
    ASSERT_EQ(device.get_harvesting_masks_for_soc_descriptors().at(0) & simulated_harvesting_masks[0], 6) << "Expected first chip to include simulated harvesting mask of 6";
    // ASSERT_EQ(device.get_harvesting_masks_for_soc_descriptors().at(1), 12) << "Expected second chip to have harvesting mask of 12";
    device.close_device();
}

TEST(SiliconDriverGS, CustomSocDesc) {
    std::set<chip_id_t> target_devices = {0};
    std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {{0, 6}, {1, 12}};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting
    tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("./tests/soc_descs/grayskull_1x1_arch.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true, false, simulated_harvesting_masks);
    auto sdesc_per_chip = device.get_virtual_soc_descriptors();
    ASSERT_EQ(device.using_harvested_soc_descriptors(), false) << "SOC descriptors should not be modified when harvesting is disabled";
    for(const auto& chip : sdesc_per_chip) {
        ASSERT_EQ(chip.second.workers.size(), 1) << "Expected 1x1 SOC descriptor to be unmodified by driver";
    }
}

TEST(SiliconDriverGS, HarvestingRuntime) {
    auto get_static_tlb_index = [] (tt_xy_pair target) {
        int flat_index = target.y * tt::umd::wormhole::GRID_SIZE_X + target.x;
        if (flat_index == 0) {
            return -1;
        }
        return flat_index;
    };

    std::set<chip_id_t> target_devices = {0};
    std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {{0, 6}, {1, 12}};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true, true, simulated_harvesting_masks);

    for(int i = 0; i < target_devices.size(); i++) {
        // Iterate over devices and only setup static TLBs for functional worker cores
        auto& sdesc = device.get_virtual_soc_descriptors().at(i);
        for(auto& core : sdesc.workers) {
            // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE. 
            device.configure_tlb(i, core, get_static_tlb_index(core), l1_mem::address_map::DATA_BUFFER_SPACE_BASE);
        }
        device.setup_core_to_tlb_map(i, get_static_tlb_index);
    }

    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> dynamic_tlb_vector_to_write = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    std::vector<uint32_t> dynamic_readback_vec = {};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float timeout_in_seconds = 10;
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space
    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
        std::uint32_t dynamic_write_address = 0x30000000;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core :  device.get_virtual_soc_descriptors().at(i).workers) {
                device.write_to_device(vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), address, "");
                device.write_to_device(vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), dynamic_write_address, "SMALL_READ_WRITE_TLB");
                auto start_time = std::chrono::high_resolution_clock::now();
                while(!(vector_to_write == readback_vec)) {
                    float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                    if(wait_duration > timeout_in_seconds) {
                        break;
                    }
                    test_utils::read_data_from_device(device, readback_vec, tt_cxy_pair(i, core), address, 40, "");
                    test_utils::read_data_from_device(device, dynamic_readback_vec, tt_cxy_pair(i, core), dynamic_write_address, 40, "SMALL_READ_WRITE_TLB");
                }
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB"); // Clear any written data
                device.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), dynamic_write_address, "SMALL_READ_WRITE_TLB"); // Clear any written data
                readback_vec = {};
                dynamic_readback_vec = {};
            }
            address += 0x20; // Increment by uint32_t size for each write
            dynamic_write_address += 0x20;
        }
    }
    device.close_device();
}

TEST(SiliconDriverGS, StaticTLB_RW) {
    auto get_static_tlb_index = [] (tt_xy_pair target) {
        int flat_index = target.y * tt::umd::wormhole::GRID_SIZE_X + target.x;
        if (flat_index == 0) {
            return -1;
        }
        return flat_index;
    };
    std::set<chip_id_t> target_devices = {0};
    
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true);
    for(int i = 0; i < target_devices.size(); i++) {
        // Iterate over devices and only setup static TLBs for worker cores
        auto& sdesc = device.get_virtual_soc_descriptors().at(i);
        for(auto& core : sdesc.workers) {
            // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE. 
            device.configure_tlb(i, core, get_static_tlb_index(core), l1_mem::address_map::DATA_BUFFER_SPACE_BASE, TLB_DATA::Posted);
        }
        device.setup_core_to_tlb_map(i, get_static_tlb_index);
    }
    
    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float timeout_in_seconds = 10;
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space
    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core :  device.get_virtual_soc_descriptors().at(i).workers) {
                device.write_to_device(vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), address, "");
                auto start_time = std::chrono::high_resolution_clock::now();
                while(!(vector_to_write == readback_vec)) {
                    float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                    if(wait_duration > timeout_in_seconds) {
                        break;
                    }
                    test_utils::read_data_from_device(device, readback_vec, tt_cxy_pair(i, core), address, 40, "");
                }
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB"); // Clear any written data
                readback_vec = {};
            }
            address += 0x20; // Increment by uint32_t size for each write
        }
    }
    device.close_device();    
}

TEST(SiliconDriverGS, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for each transaction
    std::set<chip_id_t> target_devices = {0};

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true);
    device.set_fallback_tlb_ordering_mode("SMALL_READ_WRITE_TLB", TLB_DATA::Posted); // Explicitly test API to set fallback tlb ordering mode
    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = {};
    float timeout_in_seconds = 10;

    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core : device.get_virtual_soc_descriptors().at(i).workers) {
                device.write_to_device(vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB");
                auto start_time = std::chrono::high_resolution_clock::now();
                while(!(vector_to_write == readback_vec)) {
                    float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                    if(wait_duration > timeout_in_seconds) {
                        break;
                    }
                    test_utils::read_data_from_device(device, readback_vec, tt_cxy_pair(i, core), address, 40, "SMALL_READ_WRITE_TLB");
                }

                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB"); // Clear any written data
                readback_vec = {};
            }
            address += 0x20; // Increment by uint32_t size for each write
        }
    }
    device.close_device();
}

TEST(SiliconDriverGS, MultiThreadedDevice) {
    // Have 2 threads read and write from a single device concurrently
    // All transactions go through a single Dynamic TLB. We want to make sure this is thread/process safe

    std::set<chip_id_t> target_devices = {0};

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true);
    
    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        float timeout_in_seconds = 10;
        std::uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
        for(int loop = 0; loop < 100; loop++) {
            for(auto& core : device.get_virtual_soc_descriptors().at(0).workers) {
                device.write_to_device(vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(0, core), address, "SMALL_READ_WRITE_TLB");
                auto start_time = std::chrono::high_resolution_clock::now();
                while(!(vector_to_write == readback_vec)) {
                    float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                    if(wait_duration > timeout_in_seconds) {
                        break;
                    }
                    test_utils::read_data_from_device(device, readback_vec, tt_cxy_pair(0, core), address, 40, "SMALL_READ_WRITE_TLB");
                }
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                readback_vec = {};
            }
            address += 0x20;
        }
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        float timeout_in_seconds = 10;
        std::uint32_t address = 0x30000000;
        for(auto& core_ls : device.get_virtual_soc_descriptors().at(0).dram_cores) {
            for(int loop = 0; loop < 100; loop++) {
                for(auto& core : core_ls) {
                    device.write_to_device(vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(0, core), address, "SMALL_READ_WRITE_TLB");
                    auto start_time = std::chrono::high_resolution_clock::now();
                    while(!(vector_to_write == readback_vec)) {
                        float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                        if(wait_duration > timeout_in_seconds) {
                            break;
                    }
                    test_utils::read_data_from_device(device, readback_vec, tt_cxy_pair(0, core), address, 40, "SMALL_READ_WRITE_TLB");
                }
                    ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                    readback_vec = {};
                }
                address += 0x20;
            }
        }
    });

    th1.join();
    th2.join();
    device.close_device();
}

TEST(SiliconDriverGS, MultiThreadedMemBar) { // this tests takes ~5 mins to run
    // Have 2 threads read and write from a single device concurrently
    // All (fairly large) transactions go through a static TLB. 
    // We want to make sure the memory barrier is thread/process safe.

    // Memory barrier flags get sent to address 0 for all channels in this test

     auto get_static_tlb_index = [] (tt_xy_pair target) {
        int flat_index = target.y * tt::umd::wormhole::GRID_SIZE_X + target.x;
        if (flat_index == 0) {
            return -1;
        }
        return flat_index;
    };

    std::set<chip_id_t> target_devices = {0};
    uint32_t base_addr = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    uint32_t num_host_mem_ch_per_mmio_device = 1;

    tt_SiliconDevice device = tt_SiliconDevice(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), "", target_devices, num_host_mem_ch_per_mmio_device, false, true);
    
    for(int i = 0; i < target_devices.size(); i++) {
        // Iterate over devices and only setup static TLBs for functional worker cores
        auto& sdesc = device.get_virtual_soc_descriptors().at(i);
        for(auto& core : sdesc.workers) {
            // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE. 
            device.configure_tlb(i, core, get_static_tlb_index(core), base_addr);
        }
        device.setup_core_to_tlb_map(i, get_static_tlb_index);
    }

    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();
    std::vector<uint32_t> readback_membar_vec = {};
    for(auto& core : device.get_virtual_soc_descriptors().at(0).workers) {
        test_utils::read_data_from_device(device, readback_membar_vec, tt_cxy_pair(0, core), 0, 4, "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(readback_membar_vec.at(0), 187); // Ensure that memory barriers were correctly initialized on all workers
        readback_membar_vec = {};
    }

    for(auto& core : device.get_virtual_soc_descriptors().at(0).workers) {
        test_utils::read_data_from_device(device, readback_membar_vec, tt_cxy_pair(0, core), 0, 4, "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(readback_membar_vec.at(0), 187); // Ensure that memory barriers were correctly initialized on all workers
        readback_membar_vec = {};
    }

    for(int chan = 0; chan <  device.get_virtual_soc_descriptors().at(0).get_num_dram_channels(); chan++) {
        auto core = device.get_virtual_soc_descriptors().at(0).get_core_for_dram_channel(chan, 0);
        test_utils::read_data_from_device(device, readback_membar_vec, tt_cxy_pair(0, core), 0, 4, "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(readback_membar_vec.at(0), 187); // Ensure that memory barriers were correctly initialized on all DRAM
        readback_membar_vec = {};
    }
    // Launch 2 thread accessing different locations of L1 and using memory barrier between write and read
    // Ensure now RAW race and membars are thread safe
    std::vector<uint32_t> vec1(25600);
    std::vector<uint32_t> vec2(25600);
    std::vector<uint32_t> zeros(25600, 0);

    for(int i = 0; i < vec1.size(); i++) {
        vec1.at(i) = i;
    }
    for(int i = 0; i < vec2.size(); i++) {
        vec2.at(i) = vec1.size() + i;
    }

    std::thread th1 = std::thread([&] {
        std::uint32_t address = base_addr;
        for(int loop = 0; loop < 100; loop++) {
            for(auto& core : device.get_virtual_soc_descriptors().at(0).workers) {
                std::vector<uint32_t> readback_vec = {};
                device.write_to_device(vec1.data(), vec1.size() * sizeof(std::uint32_t), tt_cxy_pair(0, core), address, "");
                device.l1_membar(0, "", {core});
                test_utils::read_data_from_device(device, readback_vec, tt_cxy_pair(0, core), address, 4*vec1.size(), "");
                ASSERT_EQ(readback_vec, vec1);
                device.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), tt_cxy_pair(0, core), address, "");
                readback_vec = {};
            }
        }
    });

    std::thread th2 = std::thread([&] {
        std::uint32_t address = base_addr + vec1.size() * 4;
        for(int loop = 0; loop < 100; loop++) {
            for(auto& core : device.get_virtual_soc_descriptors().at(0).workers) {
                std::vector<uint32_t> readback_vec = {};
                device.write_to_device(vec2.data(), vec2.size() * sizeof(std::uint32_t), tt_cxy_pair(0, core), address, "");
                device.l1_membar(0, "", {core});
                test_utils::read_data_from_device(device, readback_vec, tt_cxy_pair(0, core), address, 4*vec2.size(), "");
                ASSERT_EQ(readback_vec, vec2);
                device.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), tt_cxy_pair(0, core), address, "") ;
                readback_vec = {};
            }
        }
    });

    th1.join();
    th2.join();

    for(auto& core : device.get_virtual_soc_descriptors().at(0).workers) {
        test_utils::read_data_from_device(device, readback_membar_vec, tt_cxy_pair(0, core), 0, 4, "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(readback_membar_vec.at(0), 187); // Ensure that memory barriers end up in correct sate workers
        readback_membar_vec = {};
    }

    device.close_device();
}
