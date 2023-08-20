#include "gtest/gtest.h"
#include <tt_device.h>
#include <device/soc_descriptor.h>
#include "device_data.hpp"
#include "l1_address_map.h"
#include <thread>

TEST(SiliconDriverGS, StaticTLB_RW) {
    auto get_static_tlb_index = [] (tt_xy_pair target) {
        int flat_index = target.y * DEVICE_DATA.GRID_SIZE_X + target.x;
        if (flat_index == 0) {
            return -1;
        }
        return flat_index;
    };

    tt_SocDescriptor sdesc = *load_soc_descriptor_from_yaml("./device/grayskull_10x12.yaml");
    std::set<chip_id_t> target_devices = {0, 1};

    std::unordered_map<chip_id_t, tt_SocDescriptor> sdesc_per_chip = {};
    
    for(int i = 0; i < target_devices.size(); i++) {
        sdesc_per_chip.insert({i , sdesc});
    }

    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {}; // Don't set any dynamic TLBs in this test
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_SiliconDevice device = tt_SiliconDevice(sdesc_per_chip, "", target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);

    for(int i = 0; i < target_devices.size(); i++) {
        // Iterate over devices and only setup static TLBs for worker cores
        auto& sdesc = sdesc_per_chip.at(i);
        for(auto& core : sdesc.workers) {
            // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE. 
            device.configure_tlb(i, core, get_static_tlb_index(core), l1_mem::address_map::DATA_BUFFER_SPACE_BASE);
        } 
    }

    device.setup_core_to_tlb_map(get_static_tlb_index);
    
    tt_device_params default_params;
    device.start_device(default_params);

    for(int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float timeout_in_seconds = 10;
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space
    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core : sdesc_per_chip[i].workers) {
                device.write_to_device(vector_to_write, tt_cxy_pair(i, core), address, "");
                auto start_time = std::chrono::high_resolution_clock::now();
                while(!(vector_to_write == readback_vec)) {
                    float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                    if(wait_duration > timeout_in_seconds) {
                        break;
                    }
                    device.read_from_device(readback_vec, tt_cxy_pair(i, core), address, 40, "");
                }
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.write_to_device(zeros, tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB"); // Clear any written data
                readback_vec = {};
            }
            address += 0x20; // Increment by uint32_t size for each write
        }
    }
    device.close_device();    
}

TEST(SiliconDriverGS, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for each transaction
    tt_SocDescriptor sdesc = *load_soc_descriptor_from_yaml("./device/grayskull_10x12.yaml");
    std::set<chip_id_t> target_devices = {0, 1};

    std::unordered_map<chip_id_t, tt_SocDescriptor> sdesc_per_chip = {};

    for(int i = 0; i < target_devices.size(); i++) {
        sdesc_per_chip.insert({i , sdesc});
    }

    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    dynamic_tlb_config.insert({"SMALL_READ_WRITE_TLB", 157}); // Use this for all reads and writes to worker cores
    tt_SiliconDevice device = tt_SiliconDevice(sdesc_per_chip, "", target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);

    tt_device_params default_params;
    device.start_device(default_params);

    for(int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = {};
    float timeout_in_seconds = 10;

    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core : sdesc_per_chip[i].workers) {
                device.write_to_device(vector_to_write, tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB");
                auto start_time = std::chrono::high_resolution_clock::now();
                while(!(vector_to_write == readback_vec)) {
                    float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                    if(wait_duration > timeout_in_seconds) {
                        break;
                    }
                    device.read_from_device(readback_vec, tt_cxy_pair(i, core), address, 40, "SMALL_READ_WRITE_TLB");
                }

                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.write_to_device(zeros, tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB"); // Clear any written data
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

    tt_SocDescriptor sdesc = *load_soc_descriptor_from_yaml("./device/grayskull_10x12.yaml");
    std::set<chip_id_t> target_devices = {0};

    std::unordered_map<chip_id_t, tt_SocDescriptor> sdesc_per_chip = {};

    for(int i = 0; i < target_devices.size(); i++) {
        sdesc_per_chip.insert({i , sdesc});
    }

    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    dynamic_tlb_config.insert({"SMALL_READ_WRITE_TLB", 157}); // Use this for all reads and writes to worker cores
    tt_SiliconDevice device = tt_SiliconDevice(sdesc_per_chip, "", target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
    
    tt_device_params default_params;
    device.start_device(default_params);

    for(int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        float timeout_in_seconds = 10;
        std::uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
        for(int loop = 0; loop < 100; loop++) {
            for(auto& core : sdesc_per_chip[0].workers) {
                device.write_to_device(vector_to_write, tt_cxy_pair(0, core), address, "SMALL_READ_WRITE_TLB");
                auto start_time = std::chrono::high_resolution_clock::now();
                while(!(vector_to_write == readback_vec)) {
                    float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                    if(wait_duration > timeout_in_seconds) {
                        break;
                    }
                    device.read_from_device(readback_vec, tt_cxy_pair(0, core), address, 40, "SMALL_READ_WRITE_TLB");
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
        for(auto& core_ls : sdesc_per_chip[0].dram_cores) {
            for(int loop = 0; loop < 100; loop++) {
                for(auto& core : core_ls) {
                    device.write_to_device(vector_to_write, tt_cxy_pair(0, core), address, "SMALL_READ_WRITE_TLB");
                    auto start_time = std::chrono::high_resolution_clock::now();
                    while(!(vector_to_write == readback_vec)) {
                        float wait_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                        if(wait_duration > timeout_in_seconds) {
                            break;
                    }
                    device.read_from_device(readback_vec, tt_cxy_pair(0, core), address, 40, "SMALL_READ_WRITE_TLB");
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