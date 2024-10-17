
#include <gtest/gtest.h>
#include "fmt/xchar.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "tests/test_utils/generate_cluster_desc.hpp"

// TODO: change to tt_cluster
#include "device/tt_device.h"
#include "device/tt_cluster_descriptor.h"

// TODO: obviously we need some other way to set this up
#include "src/firmware/riscv/wormhole/host_mem_address_map.h"
#include "src/firmware/riscv/wormhole/noc/noc_parameters.h"
#include "src/firmware/riscv/wormhole/eth_interface.h"
#include "src/firmware/riscv/wormhole/l1_address_map.h"
#include "src/firmware/riscv/wormhole/eth_l1_address_map.h"

// TODO: do proper renaming.
using Cluster = tt_SiliconDevice;

// These tests are intended to be run with the same code on all kinds of systems:
// E75, E150, E300
// N150. N300
// Galaxy

// TODO: This function should not exist, the API itself should be simple enough.
std::unique_ptr<tt_ClusterDescriptor> get_cluster_descriptor() {

    // TODO: This should not be needed. And could be part of the cluster descriptor probably.
    // Note that cluster descriptor holds logical ids of chips.
    // Which are different than physical PCI ids, which are /dev/tenstorrent/N ones.
    // You have to see if physical PCIe is GS before constructing a cluster descriptor.
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    std::set<int> pci_device_ids_set (pci_device_ids.begin(), pci_device_ids.end());

    tt::ARCH device_arch = tt::ARCH::GRAYSKULL;
    if (!pci_device_ids.empty()) {
        // TODO: This should be removed from the API, the driver itself should do it.
        int physical_device_id = pci_device_ids[0];
        // TODO: remove logical_device_id
        PCIDevice pci_device (physical_device_id, 0);
        device_arch = pci_device.get_arch();
    }

    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        std::cout << "No Tenstorrent devices found. Skipping test." << std::endl;
        return nullptr;
    }

    // TODO: remove getting manually cluster descriptor from yaml.
    std::string yaml_path = test_utils::GetClusterDescYAML();
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc;
    if (device_arch == tt::ARCH::GRAYSKULL) {
        cluster_desc = tt_ClusterDescriptor::create_for_grayskull_cluster(pci_device_ids_set, pci_device_ids);
    } else {
        cluster_desc = tt_ClusterDescriptor::create_from_yaml(yaml_path);
    }

    return cluster_desc;
}

// TODO: This function should not exist, the API itself should be simple enough.
std::unique_ptr<Cluster> get_cluster() {

    // TODO: This should not be needed. And could be part of the cluster descriptor probably.
    // Note that cluster descriptor holds logical ids of chips.
    // Which are different than physical PCI ids, which are /dev/tenstorrent/N ones.
    // You have to see if physical PCIe is GS before constructing a cluster descriptor.
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    std::set<int> pci_device_ids_set (pci_device_ids.begin(), pci_device_ids.end());

    tt::ARCH device_arch = tt::ARCH::GRAYSKULL;
    if (!pci_device_ids.empty()) {
        // TODO: This should be removed from the API, the driver itself should do it.
        int physical_device_id = pci_device_ids[0];
        // TODO: remove logical_device_id
        PCIDevice pci_device (physical_device_id, 0);
        device_arch = pci_device.get_arch();
    }

    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        std::cout << "No Tenstorrent devices found. Skipping test." << std::endl;
        return nullptr;
    }

    // TODO: remove getting manually cluster descriptor from yaml.
    std::string yaml_path = test_utils::GetClusterDescYAML();
    // TODO: Remove the need to do this, allow default constructor to construct with all chips.
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = get_cluster_descriptor();
    std::unordered_set<int> detected_num_chips = cluster_desc->get_all_chips();

    // TODO: make this unordered vs set conversion not needed.
    std::set<chip_id_t> detected_num_chips_set (detected_num_chips.begin(), detected_num_chips.end());

    
    // TODO: This would be incorporated inside SocDescriptor.
    std::string soc_path;
    if (device_arch == tt::ARCH::GRAYSKULL) {
        soc_path = test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml");
    } else if (device_arch == tt::ARCH::WORMHOLE || device_arch == tt::ARCH::WORMHOLE_B0) {
        soc_path = test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml");
    } else if (device_arch == tt::ARCH::BLACKHOLE) {
        soc_path = test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml");
    } else {
        throw std::runtime_error("Unsupported architecture");
    }


    // TODO: Don't pass each of these arguments.
    return std::unique_ptr<Cluster>(new Cluster(soc_path, device_arch == tt::ARCH::GRAYSKULL ? "" : yaml_path, detected_num_chips_set));
}

// TODO: Should not be wormhole specific.
// TODO: Offer default setup for what you can.
void setup_wormhole_remote(Cluster* umd_cluster) {
    if (!umd_cluster->get_target_remote_device_ids().empty() && umd_cluster->get_soc_descriptor(*umd_cluster->get_all_chips_in_cluster().begin()).arch == tt::ARCH::WORMHOLE_B0) {
        
        // Populate address map and NOC parameters that the driver needs for remote transactions
        umd_cluster->set_driver_host_address_params({host_mem::address_map::ETH_ROUTING_BLOCK_SIZE, host_mem::address_map::ETH_ROUTING_BUFFERS_START});

        umd_cluster->set_driver_eth_interface_params({NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS, ETH_RACK_COORD_WIDTH, CMD_BUF_SIZE_MASK, MAX_BLOCK_SIZE,
                                                REQUEST_CMD_QUEUE_BASE, RESPONSE_CMD_QUEUE_BASE, CMD_COUNTERS_SIZE_BYTES, REMOTE_UPDATE_PTR_SIZE_BYTES,
                                                CMD_DATA_BLOCK, CMD_WR_REQ, CMD_WR_ACK, CMD_RD_REQ, CMD_RD_DATA, CMD_BUF_SIZE, CMD_DATA_BLOCK_DRAM, ETH_ROUTING_DATA_BUFFER_ADDR,
                                                REQUEST_ROUTING_CMD_QUEUE_BASE, RESPONSE_ROUTING_CMD_QUEUE_BASE, CMD_BUF_PTR_MASK, CMD_ORDERED, CMD_BROADCAST});
        
        umd_cluster->set_device_l1_address_params({l1_mem::address_map::NCRISC_FIRMWARE_BASE, l1_mem::address_map::FIRMWARE_BASE,
                                    l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE,
                                    l1_mem::address_map::TRISC_BASE, l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, eth_l1_mem::address_map::FW_VERSION_ADDR});

    }
}

// This test should be one line only.
TEST(ApiTest, OpenAllChips) {
    std::unique_ptr<Cluster> umd_cluster = get_cluster();
}

TEST(ApiTest, SimpleIOAllChips) {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = get_cluster_descriptor();
    std::unique_ptr<Cluster> umd_cluster = get_cluster();

    if (umd_cluster == nullptr || umd_cluster->get_all_chips_in_cluster().empty()) {
        std::cout << "No chips found. Skipping test." << std::endl;
        return;
    }

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    // TODO: this should be part of constructor if it is mandatory.
    setup_wormhole_remote(umd_cluster.get());

    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global (chip_id, any_core);

        if (cluster_desc->is_chip_remote(chip_id) && soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, any_core_global, 0, "LARGE_WRITE_TLB");
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global (chip_id, any_core);

        if (cluster_desc->is_chip_remote(chip_id) && soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), any_core_global, 0, data_size, "LARGE_READ_TLB");

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiTest, RemoteFlush) {

    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = get_cluster_descriptor();
    std::unique_ptr<Cluster> umd_cluster = get_cluster();

    if (umd_cluster == nullptr || umd_cluster->get_all_chips_in_cluster().empty()) {
        std::cout << "No chips found. Skipping test." << std::endl;
        return;
    }

    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);

    // TODO: this should be part of constructor if it is mandatory.
    setup_wormhole_remote(umd_cluster.get());

    for (auto chip_id : umd_cluster->get_target_remote_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global (chip_id, any_core);

        if (!cluster_desc->is_chip_remote(chip_id)) {
            std::cout << "Chip " << chip_id << " skipped because it is not a remote chip." << std::endl;
            continue;
        }

        if (soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;
        umd_cluster->write_to_device(data.data(), data_size, any_core_global, 0, "LARGE_WRITE_TLB");

        std::cout << "Waiting for remote chip flush " << chip_id << std::endl;
        umd_cluster->wait_for_non_mmio_flush(chip_id);

        std::cout << "Waiting again for flush " << chip_id << ", should be no-op" << std::endl;
        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    chip_id_t any_remote_chip = *umd_cluster->get_target_remote_device_ids().begin();
    const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(any_remote_chip);
    tt_xy_pair any_core = soc_desc.workers[0];
    tt_cxy_pair any_core_global (any_remote_chip, any_core);
    if (soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
        std::cout << "Skipping whole cluster wait because it is not a wormhole_b0 chip." << std::endl;
        return;
    }
    std::cout << "Writing to chip " << any_remote_chip << " core " << any_core.str() << std::endl;
    umd_cluster->write_to_device(data.data(), data_size, any_core_global, 0, "LARGE_WRITE_TLB");    

    std::cout << "Testing whole cluster wait for remote chip flush." << std::endl;
    umd_cluster->wait_for_non_mmio_flush();

    std::cout << "Testing whole cluster wait for remote chip flush again, should be no-op." << std::endl;
    umd_cluster->wait_for_non_mmio_flush();
}
