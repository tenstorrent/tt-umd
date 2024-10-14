
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

// TODO: do proper renaming.
using Cluster = tt_SiliconDevice;

// These tests are intended to be run with the same code on all kinds of systems:
// E75, E150, E300
// N150. N300
// Galaxy

// This test should be one line only.
TEST(ApiTest, OpenAllChips) {

    // TODO: This should be removed from the API, the driver itself should do it.
    std::string soc_path;
    int logical_device_id = 0;
    int physical_device_id = 0;
    PCIDevice pci_device (physical_device_id, logical_device_id);
    tt::ARCH device_arch = pci_device.get_arch();
    
    // TODO: This would be incorporated inside SocDescriptor.
    if (device_arch == tt::ARCH::GRAYSKULL) {
        soc_path = test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml");
    } else if (device_arch == tt::ARCH::WORMHOLE || device_arch == tt::ARCH::WORMHOLE_B0) {
        soc_path = test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml");
    } else if (device_arch == tt::ARCH::BLACKHOLE) {
        soc_path = test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml");
    } else {
        throw std::runtime_error("Unsupported architecture");
    }

    // TODO: Don't pass soc path to the driver.
    Cluster umd_cluster = Cluster(soc_path);
}