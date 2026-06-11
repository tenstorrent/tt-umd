// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <fmt/ranges.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "test_utils/fetch_local_files.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"

using namespace tt;
using namespace tt::umd;

namespace tt::umd::test_utils {

// Returns true if the system has remote (Ethernet-connected) chips, i.e. an N300 board.
inline bool has_remote_chips() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        return false;
    }
    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids[0]);
    tt_device->init_tt_device();

    auto board_type = tt_device->get_board_type();
    return board_type == tt::BoardType::N300;
}

// Number of host memory channels a test needs to also reach remote chips: 1 when remote chips are
// present, 0 for local-only configurations.
inline uint32_t get_num_host_ch_for_test() { return has_remote_chips() ? 1UL : 0UL; }

// Canonical way to create a Cluster in tests.
//
// The ClusterOptions default for num_host_mem_ch_per_mmio_device is std::nullopt, which makes the
// Cluster auto-determine the number of host memory channels. That value is often larger than 0 and
// allocating those channels noticeably slows tests down. Tests that don't care about host memory
// channels should go through this helper, which defaults them when the caller didn't set them:
//   - needs_sysmem == false (default): 0 channels (fastest).
//   - needs_sysmem == true: get_num_host_ch_for_test(), i.e. 0 for local-only configs and 1 when
//     remote chips are present (so the test can reach them).
// Tests that need a specific number can pass it via `options.num_host_mem_ch_per_mmio_device` and
// it is honored as-is.
//
// If TT_UMD_SIMULATOR is set, the chip type, target devices, and simulator directory are overridden
// to target the simulator.
inline std::unique_ptr<Cluster> make_default_test_cluster(ClusterOptions options = {}, bool needs_sysmem = false) {
    if (!options.num_host_mem_ch_per_mmio_device.has_value()) {
        options.num_host_mem_ch_per_mmio_device = needs_sysmem ? get_num_host_ch_for_test() : 0UL;
    }
    if (const char* sim_path = std::getenv("TT_UMD_SIMULATOR")) {
        options.chip_type = ChipType::SIMULATION;
        options.simulator_directory = std::filesystem::path(sim_path);

        // A multichip simulator (e.g. wh_x2) needs a real cluster descriptor to expose
        // its topology -- the auto-generated mock descriptor marks every chip MMIO and
        // has no eth links, so a remote chip would never be enumerated. When
        // TT_UMD_CLUSTER_DESCRIPTOR names a descriptor (a filename under
        // tests/cluster_descriptor_examples/), load it and target all of its chips.
        // Without it, behaviour is unchanged: a single MMIO chip 0.
        if (const char* desc_name = std::getenv("TT_UMD_CLUSTER_DESCRIPTOR")) {
            // Static lifetime: Cluster stores the options (including this pointer), and
            // the descriptor selection is constant for the whole test process.
            static std::unique_ptr<ClusterDescriptor> sim_cluster_desc =
                ClusterDescriptor::create_from_yaml(GetClusterDescAbsPath(desc_name));
            options.cluster_descriptor = sim_cluster_desc.get();
            options.target_devices.clear();
            for (tt::ChipId chip : sim_cluster_desc->get_all_chips()) {
                options.target_devices.insert(chip);
            }
        } else {
            options.target_devices = {0};
        }
    }
    return std::make_unique<Cluster>(options);
}

template <typename T>
static inline void size_buffer_to_capacity(std::vector<T>& data_buf, std::size_t size_in_bytes) {
    std::size_t target_size = 0;
    if (size_in_bytes > 0) {
        target_size = ((size_in_bytes - 1) / sizeof(T)) + 1;
    }
    data_buf.resize(target_size);
}

static inline void read_data_from_device(
    Cluster& cluster, std::vector<uint32_t>& vec, ChipId chip_id, CoreCoord core, uint64_t addr, uint32_t size) {
    size_buffer_to_capacity(vec, size);
    // Use architecture-specific read method: DMA for WORMHOLE_B0, regular read for others (including Blackhole).
    cluster.read_from_device(vec.data(), chip_id, core, addr, size);
}

inline void fill_with_random_bytes(uint8_t* data, size_t n) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    uint64_t* data64 = reinterpret_cast<uint64_t*>(data);
    std::generate_n(data64, n / 8, [&]() { return gen(); });

    // Handle remaining bytes.
    for (size_t i = (n / 8) * 8; i < n; ++i) {
        data[i] = static_cast<uint8_t>(gen());
    }
}

inline std::string convert_to_comma_separated_string(const std::unordered_set<int>& devices) {
    return fmt::format("{}", fmt::join(devices, ","));
}

inline bool is_iommu_available() {
    return make_default_test_cluster()->get_tt_device(0)->get_pci_device()->is_iommu_enabled();
}

inline bool is_virtual_machine() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("flags") != std::string::npos && line.find("hypervisor") != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace tt::umd::test_utils
