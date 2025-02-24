// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <thread>

#include "eth_l1_address_map.h"
#include "gtest/gtest.h"
#include "host_mem_address_map.h"
#include "l1_address_map.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/wormhole_implementation.h"

using namespace tt::umd;

constexpr std::uint32_t DRAM_BARRIER_BASE = 0;

static void set_barrier_params(Cluster& cluster) {
    // Populate address map and NOC parameters that the driver needs for memory barriers and remote transactions.
    cluster.set_barrier_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, DRAM_BARRIER_BASE});
}

std::int32_t get_static_tlb_index(tt_xy_pair target) {
    bool is_eth_location =
        std::find(std::cbegin(tt::umd::wormhole::ETH_LOCATIONS), std::cend(tt::umd::wormhole::ETH_LOCATIONS), target) !=
        std::cend(tt::umd::wormhole::ETH_LOCATIONS);
    bool is_tensix_location =
        std::find(
            std::cbegin(tt::umd::wormhole::T6_X_LOCATIONS), std::cend(tt::umd::wormhole::T6_X_LOCATIONS), target.x) !=
            std::cend(tt::umd::wormhole::T6_X_LOCATIONS) &&
        std::find(
            std::cbegin(tt::umd::wormhole::T6_Y_LOCATIONS), std::cend(tt::umd::wormhole::T6_Y_LOCATIONS), target.y) !=
            std::cend(tt::umd::wormhole::T6_Y_LOCATIONS);
    if (is_eth_location) {
        if (target.y == 6) {
            target.y = 1;
        }

        if (target.x >= 5) {
            target.x -= 1;
        }
        target.x -= 1;

        int flat_index = target.y * 8 + target.x;
        int tlb_index = flat_index;
        return tlb_index;

    } else if (is_tensix_location) {
        if (target.x >= 5) {
            target.x -= 1;
        }
        target.x -= 1;

        if (target.y >= 6) {
            target.y -= 1;
        }
        target.y -= 1;

        int flat_index = target.y * 8 + target.x;

        // All 80 get single 1MB TLB.
        int tlb_index = tt::umd::wormhole::ETH_LOCATIONS.size() + flat_index;

        return tlb_index;
    } else {
        return -1;
    }
}

std::set<chip_id_t> get_target_devices() {
    std::set<chip_id_t> target_devices;
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc_uniq = tt_ClusterDescriptor::create();
    for (int i = 0; i < cluster_desc_uniq->get_number_of_chips(); i++) {
        target_devices.insert(i);
    }
    return target_devices;
}

TEST(SiliconDriverWH, CreateDestroy) {
    std::set<chip_id_t> target_devices = get_target_devices();
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_device_params default_params;
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting
    for (int i = 0; i < 50; i++) {
        Cluster cluster = Cluster(
            test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_1x1.yaml"),
            target_devices,
            num_host_mem_ch_per_mmio_device,
            false,
            true,
            false);
        set_barrier_params(cluster);
        cluster.start_device(default_params);
        cluster.close_device();
    }
}

TEST(SiliconDriverWH, Harvesting) {
    std::set<chip_id_t> target_devices = get_target_devices();
    int num_devices = target_devices.size();
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {{0, {30, 0, 0}}, {1, {60, 0, 0}}};

    for (auto chip : target_devices) {
        if (!simulated_harvesting_masks.count(chip)) {
            simulated_harvesting_masks[chip] = {60, 0, 0};
        }
    }

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true, simulated_harvesting_masks);
    auto sdesc_per_chip = cluster.get_virtual_soc_descriptors();

    // Real harvesting info on this system will be forcefully included in the harvesting mask.
    std::unordered_map<chip_id_t, std::uint32_t> harvesting_info =
        cluster.get_cluster_description()->get_harvesting_info();
    for (int i = 0; i < num_devices; i++) {
        uint32_t harvesting_mask_logical =
            CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, harvesting_info.at(i));
        simulated_harvesting_masks[i].tensix_harvesting_mask |= harvesting_mask_logical;
    }

    ASSERT_EQ(cluster.using_harvested_soc_descriptors(), true) << "Expected Driver to have performed harvesting";

    for (const auto& chip : sdesc_per_chip) {
        ASSERT_LE(chip.second.get_cores(CoreType::TENSIX).size(), 48)
            << "Expected SOC descriptor with harvesting to have 48 workers or less for chip" << chip.first;
    }
    for (int i = 0; i < num_devices; i++) {
        // harvesting info stored in soc descriptor is in logical coordinates.
        ASSERT_EQ(
            cluster.get_soc_descriptor(i).harvesting_masks.tensix_harvesting_mask,
            simulated_harvesting_masks.at(i).tensix_harvesting_mask)
            << "Expecting chip " << i << " to have harvesting mask of "
            << simulated_harvesting_masks.at(i).tensix_harvesting_mask;

        // get_harvesting_masks_for_soc_descriptors will return harvesting info in noc0 coordinates.
        simulated_harvesting_masks[i].tensix_harvesting_mask =
            CoordinateManager::shuffle_tensix_harvesting_mask_to_noc0_coords(
                tt::ARCH::WORMHOLE_B0, simulated_harvesting_masks[i].tensix_harvesting_mask);
        ASSERT_EQ(
            cluster.get_harvesting_masks_for_soc_descriptors().at(i) &
                simulated_harvesting_masks.at(i).tensix_harvesting_mask,
            simulated_harvesting_masks.at(i).tensix_harvesting_mask)
            << "Expecting chip " << i << " to give noc0 harvesting mask of "
            << simulated_harvesting_masks.at(i).tensix_harvesting_mask;
    }
}

TEST(SiliconDriverWH, CustomSocDesc) {
    std::set<chip_id_t> target_devices = get_target_devices();
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {{0, {30, 0, 0}}, {1, {60, 0, 0}}};

    for (auto chip : target_devices) {
        if (!simulated_harvesting_masks.count(chip)) {
            simulated_harvesting_masks[chip] = {60, 0, 0};
        }
    }

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting
    Cluster cluster = Cluster(
        test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_1x1.yaml"),
        target_devices,
        num_host_mem_ch_per_mmio_device,
        false,
        true,
        false,
        simulated_harvesting_masks);
    auto sdesc_per_chip = cluster.get_virtual_soc_descriptors();

    ASSERT_EQ(cluster.using_harvested_soc_descriptors(), false)
        << "SOC descriptors should not be modified when harvesting is disabled";
    for (const auto& chip : sdesc_per_chip) {
        ASSERT_EQ(chip.second.get_cores(CoreType::TENSIX).size(), 1)
            << "Expected 1x1 SOC descriptor to be unmodified by driver";
    }
}

TEST(SiliconDriverWH, HarvestingRuntime) {
    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

    std::set<chip_id_t> target_devices = get_target_devices();
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {{0, {30, 0, 0}}, {1, {60, 0, 0}}};

    for (auto chip : target_devices) {
        if (!simulated_harvesting_masks.count(chip)) {
            simulated_harvesting_masks[chip] = {60, 0, 0};
        }
    }

    uint32_t num_host_mem_ch_per_mmio_device = 1;

    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true, simulated_harvesting_masks);
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    for (int i = 0; i < target_devices.size(); i++) {
        // Iterate over MMIO devices and only setup static TLBs for worker cores
        if (std::find(mmio_devices.begin(), mmio_devices.end(), i) != mmio_devices.end()) {
            auto& sdesc = cluster.get_soc_descriptor(i);
            for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
                // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
                cluster.configure_tlb(
                    i,
                    core,
                    get_static_tlb_index_callback(sdesc.translate_coord_to(core, CoordSystem::VIRTUAL)),
                    l1_mem::address_map::NCRISC_FIRMWARE_BASE);
            }
        }
    }

    tt_device_params default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> dynamic_readback_vec = {};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    for (int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        std::uint32_t dynamic_write_address = 0x40000000;
        for (int loop = 0; loop < 100;
             loop++) {  // Write to each core a 100 times at different statically mapped addresses
            for (const CoreCoord& core : cluster.get_soc_descriptor(i).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), i, core, address, "");
                cluster.write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    dynamic_write_address,
                    "SMALL_READ_WRITE_TLB");
                cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

                test_utils::read_data_from_device(cluster, readback_vec, i, core, address, 40, "");
                test_utils::read_data_from_device(
                    cluster, dynamic_readback_vec, i, core, dynamic_write_address, 40, "SMALL_READ_WRITE_TLB");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                ASSERT_EQ(vector_to_write, dynamic_readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                cluster.wait_for_non_mmio_flush();

                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    dynamic_write_address,
                    "SMALL_READ_WRITE_TLB");  // Clear any written data
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    address,
                    "");  // Clear any written data
                cluster.wait_for_non_mmio_flush();
                readback_vec = {};
                dynamic_readback_vec = {};
            }
            address += 0x20;  // Increment by uint32_t size for each write
            dynamic_write_address += 0x20;
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, UnalignedStaticTLB_RW) {
    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

    std::set<chip_id_t> target_devices = get_target_devices();
    int num_devices = target_devices.size();

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true);
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    for (int i = 0; i < target_devices.size(); i++) {
        // Iterate over MMIO devices and only setup static TLBs for worker cores
        if (std::find(mmio_devices.begin(), mmio_devices.end(), i) != mmio_devices.end()) {
            auto& sdesc = cluster.get_soc_descriptor(i);
            for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
                // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
                cluster.configure_tlb(
                    i,
                    core,
                    get_static_tlb_index_callback(sdesc.translate_coord_to(core, CoordSystem::VIRTUAL)),
                    l1_mem::address_map::NCRISC_FIRMWARE_BASE);
            }
        }
    }

    tt_device_params default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> unaligned_sizes = {3, 14, 21, 255, 362, 430, 1022, 1023, 1025};
    for (int i = 0; i < num_devices; i++) {
        for (const auto& size : unaligned_sizes) {
            std::vector<uint8_t> write_vec(size, 0);
            for (int i = 0; i < size; i++) {
                write_vec[i] = size + i;
            }
            std::vector<uint8_t> readback_vec(size, 0);
            std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
            for (int loop = 0; loop < 50; loop++) {
                for (const CoreCoord& core : cluster.get_soc_descriptor(i).get_cores(CoreType::TENSIX)) {
                    cluster.write_to_device(write_vec.data(), size, i, core, address, "");
                    cluster.wait_for_non_mmio_flush();
                    cluster.read_from_device(readback_vec.data(), i, core, address, size, "");
                    ASSERT_EQ(readback_vec, write_vec);
                    readback_vec = std::vector<uint8_t>(size, 0);
                    cluster.write_to_sysmem(write_vec.data(), size, 0, 0, 0);
                    cluster.read_from_sysmem(readback_vec.data(), 0, 0, size, 0);
                    ASSERT_EQ(readback_vec, write_vec);
                    readback_vec = std::vector<uint8_t>(size, 0);
                    cluster.wait_for_non_mmio_flush();
                }
                address += 0x20;
            }
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, StaticTLB_RW) {
    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

    std::set<chip_id_t> target_devices = get_target_devices();

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true);
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    for (int i = 0; i < target_devices.size(); i++) {
        // Iterate over MMIO devices and only setup static TLBs for worker cores
        if (std::find(mmio_devices.begin(), mmio_devices.end(), i) != mmio_devices.end()) {
            auto& sdesc = cluster.get_soc_descriptor(i);
            for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
                // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
                cluster.configure_tlb(
                    i,
                    core,
                    get_static_tlb_index_callback(sdesc.translate_coord_to(core, CoordSystem::VIRTUAL)),
                    l1_mem::address_map::NCRISC_FIRMWARE_BASE);
            }
        }
    }

    tt_device_params default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space
    for (int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        // Write to each core a 100 times at different statically mapped addresses
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(i).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), i, core, address, "");
                // Barrier to ensure that all writes over ethernet were commited
                cluster.wait_for_non_mmio_flush();
                test_utils::read_data_from_device(cluster, readback_vec, i, core, address, 40, "");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                cluster.wait_for_non_mmio_flush();
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    address,
                    "SMALL_READ_WRITE_TLB");  // Clear any written data
                cluster.wait_for_non_mmio_flush();
                readback_vec = {};
            }
            address += 0x20;  // Increment by uint32_t size for each write
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for
    // each transaction
    std::set<chip_id_t> target_devices = get_target_devices();

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true);

    set_barrier_params(cluster);

    tt_device_params default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = {};

    for (int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        // Write to each core a 100 times at different statically mapped addresses
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(i).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    address,
                    "SMALL_READ_WRITE_TLB");
                // Barrier to ensure that all writes over ethernet were commited
                cluster.wait_for_non_mmio_flush();
                test_utils::read_data_from_device(cluster, readback_vec, i, core, address, 40, "SMALL_READ_WRITE_TLB");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                cluster.wait_for_non_mmio_flush();
                cluster.write_to_device(
                    zeros.data(), zeros.size() * sizeof(std::uint32_t), i, core, address, "SMALL_READ_WRITE_TLB");
                cluster.wait_for_non_mmio_flush();
                readback_vec = {};
            }
            address += 0x20;  // Increment by uint32_t size for each write
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, MultiThreadedDevice) {
    // Have 2 threads read and write from a single device concurrently
    // All transactions go through a single Dynamic TLB. We want to make sure this is thread/process safe

    std::set<chip_id_t> target_devices = get_target_devices();

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true);

    set_barrier_params(cluster);

    tt_device_params default_params;
    cluster.start_device(default_params);

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    0,
                    core,
                    address,
                    "SMALL_READ_WRITE_TLB");
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 40, "SMALL_READ_WRITE_TLB");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
            address += 0x20;
        }
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x30000000;
        for (const std::vector<CoreCoord>& core_ls : cluster.get_soc_descriptor(0).get_dram_cores()) {
            for (int loop = 0; loop < 100; loop++) {
                for (const CoreCoord& core : core_ls) {
                    cluster.write_to_device(
                        vector_to_write.data(),
                        vector_to_write.size() * sizeof(std::uint32_t),
                        0,
                        core,
                        address,
                        "SMALL_READ_WRITE_TLB");
                    test_utils::read_data_from_device(
                        cluster, readback_vec, 0, core, address, 40, "SMALL_READ_WRITE_TLB");
                    ASSERT_EQ(vector_to_write, readback_vec)
                        << "Vector read back from core " << core.str() << " does not match what was written";
                    readback_vec = {};
                }
                address += 0x20;
            }
        }
    });

    th1.join();
    th2.join();
    cluster.close_device();
}

TEST(SiliconDriverWH, MultiThreadedMemBar) {
    // Have 2 threads read and write from a single device concurrently
    // All (fairly large) transactions go through a static TLB.
    // We want to make sure the memory barrier is thread/process safe.

    // Memory barrier flags get sent to address 0 for all channels in this test
    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

    std::set<chip_id_t> target_devices = get_target_devices();
    uint32_t base_addr = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    uint32_t num_host_mem_ch_per_mmio_device = 1;

    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true);
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    for (int i = 0; i < target_devices.size(); i++) {
        // Iterate over devices and only setup static TLBs for functional worker cores
        if (std::find(mmio_devices.begin(), mmio_devices.end(), i) != mmio_devices.end()) {
            auto& sdesc = cluster.get_soc_descriptor(i);
            for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
                // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE.
                cluster.configure_tlb(
                    i,
                    core,
                    get_static_tlb_index_callback(sdesc.translate_coord_to(core, CoordSystem::VIRTUAL)),
                    base_addr);
            }
        }
    }

    tt_device_params default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> readback_membar_vec = {};
    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, l1_mem::address_map::L1_BARRIER_BASE, 4, "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers were correctly initialized on all workers
        readback_membar_vec = {};
    }

    for (int chan = 0; chan < cluster.get_soc_descriptor(0).get_num_dram_channels(); chan++) {
        CoreCoord core = cluster.get_soc_descriptor(0).get_dram_core_for_channel(chan, 0, CoordSystem::VIRTUAL);
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, tt_cxy_pair(0, core), 0, 4, "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers were correctly initialized on all DRAM
        readback_membar_vec = {};
    }

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::ETH)) {
        test_utils::read_data_from_device(
            cluster,
            readback_membar_vec,
            0,
            core,
            eth_l1_mem::address_map::ERISC_BARRIER_BASE,
            4,
            "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(
            readback_membar_vec.at(0),
            187);  // Ensure that memory barriers were correctly initialized on all ethernet cores
        readback_membar_vec = {};
    }

    // Launch 2 thread accessing different locations of L1 and using memory barrier between write and read
    // Ensure now RAW race and membars are thread safe
    std::vector<uint32_t> vec1(2560);
    std::vector<uint32_t> vec2(2560);
    std::vector<uint32_t> zeros(2560, 0);

    for (int i = 0; i < vec1.size(); i++) {
        vec1.at(i) = i;
    }
    for (int i = 0; i < vec2.size(); i++) {
        vec2.at(i) = vec1.size() + i;
    }
    std::thread th1 = std::thread([&] {
        std::uint32_t address = base_addr;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                std::vector<uint32_t> readback_vec = {};
                cluster.write_to_device(vec1.data(), vec1.size() * sizeof(std::uint32_t), 0, core, address, "");
                cluster.l1_membar(0, {core}, "SMALL_READ_WRITE_TLB");
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 4 * vec1.size(), "");
                ASSERT_EQ(readback_vec, vec1);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), 0, core, address, "");
                readback_vec = {};
            }
        }
    });

    std::thread th2 = std::thread([&] {
        std::uint32_t address = base_addr + vec1.size() * 4;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                std::vector<uint32_t> readback_vec = {};
                cluster.write_to_device(vec2.data(), vec2.size() * sizeof(std::uint32_t), 0, core, address, "");
                cluster.l1_membar(0, {core}, "SMALL_READ_WRITE_TLB");
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 4 * vec2.size(), "");
                ASSERT_EQ(readback_vec, vec2);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), 0, core, address, "");
                readback_vec = {};
            }
        }
    });

    th1.join();
    th2.join();

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, l1_mem::address_map::L1_BARRIER_BASE, 4, "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers end up in the correct sate for workers
        readback_membar_vec = {};
    }

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::ETH)) {
        test_utils::read_data_from_device(
            cluster,
            readback_membar_vec,
            0,
            core,
            eth_l1_mem::address_map::ERISC_BARRIER_BASE,
            4,
            "SMALL_READ_WRITE_TLB");
        ASSERT_EQ(
            readback_membar_vec.at(0),
            187);  // Ensure that memory barriers end up in the correct sate for ethernet cores
        readback_membar_vec = {};
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, BroadcastWrite) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly
    std::set<chip_id_t> target_devices = get_target_devices();

    uint32_t num_host_mem_ch_per_mmio_device = 1;

    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true);
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    tt_device_params default_params;
    cluster.start_device(default_params);
    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    std::set<uint32_t> rows_to_exclude = {0, 6};
    std::set<uint32_t> cols_to_exclude = {0, 5};
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {};
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {1, 2, 3, 4, 6, 7, 8, 9};

    for (const auto& size : broadcast_sizes) {
        std::vector<uint32_t> vector_to_write(size);
        std::vector<uint32_t> zeros(size);
        std::vector<uint32_t> readback_vec = {};
        for (int i = 0; i < size; i++) {
            vector_to_write[i] = i;
            zeros[i] = 0;
        }
        // Broadcast to Tensix
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude,
            cols_to_exclude,
            "LARGE_WRITE_TLB");
        // Broadcast to DRAM
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast,
            "LARGE_WRITE_TLB");
        cluster.wait_for_non_mmio_flush();

        for (const auto i : target_devices) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(i).get_cores(CoreType::TENSIX)) {
                if (rows_to_exclude.find(core.y) != rows_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, i, core, address, vector_to_write.size() * 4, "LARGE_READ_TLB");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was broadcasted";
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    address,
                    "LARGE_WRITE_TLB");  // Clear any written data
                readback_vec = {};
            }
            for (int chan = 0; chan < cluster.get_soc_descriptor(i).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(i).get_dram_core_for_channel(chan, 0, CoordSystem::VIRTUAL);
                test_utils::read_data_from_device(
                    cluster, readback_vec, i, core, address, vector_to_write.size() * 4, "LARGE_READ_TLB");
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from DRAM core " << i << " " << core.str()
                                                         << " does not match what was broadcasted " << size;
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    address,
                    "LARGE_WRITE_TLB");  // Clear any written data
                readback_vec = {};
            }
        }
        // Wait for data to be cleared before writing next block
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, VirtualCoordinateBroadcast) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly
    std::set<chip_id_t> target_devices = get_target_devices();

    uint32_t num_host_mem_ch_per_mmio_device = 1;

    Cluster cluster = Cluster(num_host_mem_ch_per_mmio_device, false, true, true);
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    tt_device_params default_params;
    cluster.start_device(default_params);
    auto eth_version = cluster.get_ethernet_fw_version();
    bool virtual_bcast_supported =
        (eth_version >= tt_version(6, 8, 0) || eth_version == tt_version(6, 7, 241)) && cluster.translation_tables_en;
    if (!virtual_bcast_supported) {
        cluster.close_device();
        GTEST_SKIP() << "SiliconDriverWH.VirtualCoordinateBroadcast skipped since ethernet version does not support "
                        "Virtual Coordinate Broadcast or NOC translation is not enabled";
    }

    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    std::set<uint32_t> rows_to_exclude = {0, 3, 5, 6, 8, 9};
    std::set<uint32_t> cols_to_exclude = {0, 5};
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {};
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {1, 2, 3, 4, 6, 7, 8, 9};

    for (const auto& size : broadcast_sizes) {
        std::vector<uint32_t> vector_to_write(size);
        std::vector<uint32_t> zeros(size);
        std::vector<uint32_t> readback_vec = {};
        for (int i = 0; i < size; i++) {
            vector_to_write[i] = i;
            zeros[i] = 0;
        }
        // Broadcast to Tensix
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude,
            cols_to_exclude,
            "LARGE_WRITE_TLB");
        // Broadcast to DRAM
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast,
            "LARGE_WRITE_TLB");
        cluster.wait_for_non_mmio_flush();

        for (const auto i : target_devices) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(i).get_cores(CoreType::TENSIX)) {
                // Rows are excluded according to virtual coordinates, so we have to translate to that system before
                // accessing .y coordinate.
                const CoreCoord virtual_core =
                    cluster.get_soc_descriptor(i).translate_coord_to(core, CoordSystem::VIRTUAL);
                if (rows_to_exclude.find(virtual_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, i, core, address, vector_to_write.size() * 4, "LARGE_READ_TLB");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was broadcasted";
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    address,
                    "LARGE_WRITE_TLB");  // Clear any written data
                readback_vec = {};
            }
            for (int chan = 0; chan < cluster.get_soc_descriptor(i).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(i).get_dram_core_for_channel(chan, 0, CoordSystem::VIRTUAL);
                test_utils::read_data_from_device(
                    cluster, readback_vec, i, core, address, vector_to_write.size() * 4, "LARGE_READ_TLB");
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from DRAM core " << i << " " << core.str()
                                                         << " does not match what was broadcasted " << size;
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    i,
                    core,
                    address,
                    "LARGE_WRITE_TLB");  // Clear any written data
                readback_vec = {};
            }
        }
        // Wait for data to be cleared before writing next block
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

/**
 * This is a basic DMA test -- not using the PCIe controller's DMA engine, but
 * rather using the ability of the NOC to access the host system bus via traffic
 * to the PCIe block.
 *
 * sysmem means memory in the host that has been mapped for device access.  It
 * is currently one or more 1G huge pages, although this may change.
 *
 * 1. Fills sysmem with a random pattern.
 * 2. Uses PCIe block on WH to read sysmem into buffer.
 * 3. Verifies that buffer matches sysmem.
 * 4. Fills buffer with a random pattern.
 * 5. Uses PCIe block on WH to write buffer into sysmem.
 * 6. Verifies that sysmem matches buffer.
 *
 * This uses a small size for speed purposes.
 *
 * If/when we move to using IOMMU to map userspace memory for device access,
 * the technique below is a straightforward way to test that hardware can access
 * the buffer(s).
 */
TEST(SiliconDriverWH, SysmemTestWithPcie) {
    auto target_devices = get_target_devices();

    Cluster cluster(
        1,      // one "host memory channel", currently a 1G huge page
        false,  // skip driver allocs - no (don't skip)
        true,   // clean system resources - yes
        true);  // perform harvesting - yes

    set_barrier_params(cluster);
    cluster.start_device(tt_device_params{});  // no special parameters

    const chip_id_t mmio_chip_id = 0;
    const auto PCIE = cluster.get_soc_descriptor(mmio_chip_id).get_cores(CoreType::PCIE).at(0);
    const tt_cxy_pair PCIE_CORE(mmio_chip_id, PCIE.x, PCIE.y);
    const size_t test_size_bytes = 0x4000;  // Arbitrarilly chosen, but small size so the test runs quickly.

    // PCIe core is at (x=0, y=3) on Wormhole NOC0.
    ASSERT_EQ(PCIE.x, 0);
    ASSERT_EQ(PCIE.y, 3);

    // Bad API: how big is the buffer?  How do we know it's big enough?
    // Situation today is that there's a 1G hugepage behind it, although this is
    // unclear from the API and may change in the future.
    uint8_t* sysmem = (uint8_t*)cluster.host_dma_address(0, 0, 0);
    ASSERT_NE(sysmem, nullptr);

    // This is the address inside the Wormhole PCIe block that is mapped to the
    // system bus.  In Wormhole, this is a fixed address, 0x8'0000'0000.
    // The driver should have mapped this address to the bottom of sysmem.
    uint64_t base_address = cluster.get_pcie_base_addr_from_device(mmio_chip_id);

    // Buffer that we will use to read sysmem into, then write sysmem from.
    std::vector<uint8_t> buffer(test_size_bytes, 0x0);

    // Step 1: Fill sysmem with random bytes.
    test_utils::fill_with_random_bytes(sysmem, test_size_bytes);

    // Step 2: Read sysmem into buffer.
    cluster.read_from_device(&buffer[0], PCIE_CORE, base_address, buffer.size(), "REG_TLB");

    // Step 3: Verify that buffer matches sysmem.
    ASSERT_EQ(buffer, std::vector<uint8_t>(sysmem, sysmem + test_size_bytes));

    // Step 4: Fill buffer with random bytes.
    test_utils::fill_with_random_bytes(&buffer[0], test_size_bytes);

    // Step 5: Write buffer into sysmem, overwriting what was there.
    cluster.write_to_device(&buffer[0], buffer.size(), PCIE_CORE, base_address, "REG_TLB");

    // Step 5b: Read back sysmem into a throwaway buffer.  The intent is to
    // ensure the write has completed before we check sysmem against buffer.
    std::vector<uint8_t> throwaway(test_size_bytes, 0x0);
    cluster.read_from_device(&throwaway[0], PCIE_CORE, base_address, throwaway.size(), "REG_TLB");

    // Step 6: Verify that sysmem matches buffer.
    ASSERT_EQ(buffer, std::vector<uint8_t>(sysmem, sysmem + test_size_bytes));
}

/**
 * Same idea as above, but with four channels of sysmem and random addresses.
 * The hardware mechanism is too slow to sweep the entire range.
 */
TEST(SiliconDriverWH, RandomSysmemTestWithPcie) {
    const size_t num_channels = 2;  // ideally 4, but CI seems to have 2...
    auto target_devices = get_target_devices();

    Cluster cluster(
        test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"),
        target_devices,
        num_channels,
        false,  // skip driver allocs - no (don't skip)
        true,   // clean system resources - yes
        true);  // perform harvesting - yes

    set_barrier_params(cluster);
    cluster.start_device(tt_device_params{});  // no special parameters

    const chip_id_t mmio_chip_id = 0;
    const auto PCIE = cluster.get_soc_descriptor(mmio_chip_id).get_cores(CoreType::PCIE).at(0);
    const tt_cxy_pair PCIE_CORE(mmio_chip_id, PCIE.x, PCIE.y);
    const size_t ONE_GIG = 1 << 30;
    const size_t num_tests = 0x20000;  // runs in a reasonable amount of time

    // PCIe core is at (x=0, y=3) on Wormhole NOC0.
    ASSERT_EQ(PCIE.x, 0);
    ASSERT_EQ(PCIE.y, 3);

    const uint64_t ALIGNMENT = sizeof(uint32_t);
    auto generate_aligned_address = [&](uint64_t lo, uint64_t hi) -> uint64_t {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis(lo / ALIGNMENT, hi / ALIGNMENT);
        return dis(gen) * ALIGNMENT;
    };

    uint64_t base_address = cluster.get_pcie_base_addr_from_device(mmio_chip_id);
    for (size_t channel = 0; channel < num_channels; ++channel) {
        uint8_t* sysmem = (uint8_t*)cluster.host_dma_address(0, 0, channel);
        ASSERT_NE(sysmem, nullptr);

        test_utils::fill_with_random_bytes(sysmem, ONE_GIG);

        uint64_t lo = (ONE_GIG * channel);
        uint64_t hi = (lo + ONE_GIG) - 1;

        if (channel == 3) {
            // TODO: I thought everything past 0xffff'dddd was registers or
            // something, but a) I don't know what's actually there, and b)
            // the unusable range seems to be bigger than that... so
            // restricting to 0x8'f000'0000.
            hi &= ~0x0fff'ffffULL;
        }

        for (size_t i = 0; i < num_tests; ++i) {
            uint64_t address = generate_aligned_address(lo, hi);
            uint64_t noc_addr = base_address + address;
            uint64_t sysmem_address = address - lo;

            ASSERT_GE(address, lo) << "Address too low";
            ASSERT_LE(address, hi) << "Address too high";
            ASSERT_EQ(address % ALIGNMENT, 0) << "Address not properly aligned";

            uint32_t value = 0;
            cluster.read_from_device(&value, PCIE_CORE, noc_addr, sizeof(uint32_t), "LARGE_READ_TLB");

            uint32_t expected = *reinterpret_cast<uint32_t*>(&sysmem[sysmem_address]);
            ASSERT_EQ(value, expected) << fmt::format("Mismatch at address {:#x}", address);
        }
    }
}

TEST(SiliconDriverWH, LargeAddressTlb) {
    const size_t num_channels = 1;
    auto target_devices = get_target_devices();

    Cluster cluster(
        test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"),
        target_devices,
        num_channels,
        false,  // skip driver allocs - no (don't skip)
        true,   // clean system resources - yes
        true);  // perform harvesting - yes

    const tt_xy_pair ARC_CORE = cluster.get_soc_descriptor(0).get_cores(CoreType::ARC).at(0);
    const tt_cxy_pair ARC_CORE_CHIP(0, ARC_CORE.x, ARC_CORE.y);

    set_barrier_params(cluster);
    cluster.start_device(tt_device_params{});

    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return 0; };

    // Address of the reset unit in ARC core:
    uint64_t arc_reset_noc = 0x880030000ULL;

    // Offset to the scratch registers in the reset unit:
    uint64_t scratch_offset = 0x60;

    // Map a TLB to the reset unit in ARC core:
    cluster.configure_tlb(0, ARC_CORE, 0, arc_reset_noc);

    // Address of the scratch register in the reset unit:
    uint64_t addr = arc_reset_noc + scratch_offset;

    uint32_t value0 = 0;
    uint32_t value1 = 0;
    uint32_t value2 = 0;

    // Read the scratch register via BAR0:
    value0 = cluster.bar_read32(0, 0x1ff30060);

    // Read the scratch register via the TLB:
    cluster.read_from_device(&value1, ARC_CORE_CHIP, addr, sizeof(uint32_t), "LARGE_READ_TLB");

    // Read the scratch register via a different TLB, different code path:
    cluster.read_from_device(&value2, ARC_CORE_CHIP, addr, sizeof(uint32_t), "REG_TLB");

    // Mask off lower 16 bits; FW changes these dynamically:
    value0 &= 0xffff0000;
    value1 &= 0xffff0000;
    value2 &= 0xffff0000;

    // Check that the values are the same:
    EXPECT_EQ(value1, value0);
    EXPECT_EQ(value2, value0);
}
