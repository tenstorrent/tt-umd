
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "device/pcie/pci_device.hpp"

static std::vector<int> simple_pcie_device_enumeration()
{
    std::vector<int> device_ids;
    std::string path = "/dev/tenstorrent/";

    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        std::string filename = entry.path().filename().string();

        // TODO: this will skip any device that has a non-numeric name, which
        // is probably what we want longer-term (i.e. a UUID or something).
        if (std::all_of(filename.begin(), filename.end(), ::isdigit)) {
            device_ids.push_back(std::stoi(filename));
        }
    }

    std::sort(device_ids.begin(), device_ids.end());
    return device_ids;
}

TEST(PcieDeviceTest, Numa) {
    std::vector<int> nodes;

    for (auto device_id : simple_pcie_device_enumeration()) {
        PCIDevice device(device_id, 0);
        nodes.push_back(device.numa_node);
    }

    // Acceptable outcomes:
    // 1. all of them are -1 (not a NUMA system)
    // 2. all of them are >= 0 (NUMA system)
    // 3. empty vector (no devices enumerated)

    if (!nodes.empty()) {
        bool all_negative_one = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node == -1; });
        bool all_non_negative = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node >= 0; });

        EXPECT_TRUE(all_negative_one || all_non_negative)
            << "NUMA nodes should either all be -1 (non-NUMA system) or all be non-negative (NUMA system)";
    } else {
        SUCCEED() << "No PCIe devices were enumerated";
    }
}