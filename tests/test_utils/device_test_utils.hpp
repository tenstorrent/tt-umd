// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <fmt/ranges.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "tt-umd/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/types/core_coordinates.hpp"

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

inline ClusterOptions get_default_sim_cluster_options(
    const std::filesystem::path& simulator_directory,
    std::optional<uint32_t> num_host_mem_ch_per_mmio_device = std::nullopt,
    ClusterOptions options = {}) {
    options.chip_type = ChipType::SIMULATION;
    options.target_devices = {0};
    options.simulator_directory = simulator_directory;
    if (num_host_mem_ch_per_mmio_device.has_value()) {
        options.num_host_mem_ch_per_mmio_device = num_host_mem_ch_per_mmio_device;
    }
    return options;
}

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
        // A multichip simulator's topology is auto-discovered by the Cluster from a cluster_descriptor.yaml
        // placed beside the .so (see SimulationChip::get_cluster_descriptor_path_from_simulator_path); no
        // test-side env var is needed.
        options = get_default_sim_cluster_options(sim_path, std::nullopt, std::move(options));
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
    const auto devices_info = PCIDevice::enumerate_devices_info();
    return !devices_info.empty() && PCIDevice::detect_iommu(devices_info.begin()->second);
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

// The tensix cores to read back after a broadcast on one chip. Reading back every targeted core
// multiplies out with the number of broadcast sizes and the number of chips, which on an all-MMIO
// Galaxy leaves the broadcast tests among the slowest in the suite while re-checking the same
// rectangle on all 32 chips. Instead walk a staircase through the target rectangle: pair the i-th
// target row with the i-th target column, cycling the shorter axis. That reads back every row and
// every column the broadcast should reach, so a row or column strip wrongly dropped or added by the
// exclusion masks still fails, at max(rows, columns) readbacks rather than rows x columns. Taking a
// contiguous run of cores instead would not do: get_cores() is row major, so a short run collapses
// onto a couple of rows and leaves whole columns unread - including the ones flanking an excluded
// column, which is where an off-by-one in the masks shows up.
//
// `rows_to_exclude` and `cols_to_exclude` are the masks handed to the broadcast, expressed in
// `exclusion_coord_system`. Returned cores are in the SocDescriptor's default coordinate system.
inline std::vector<CoreCoord> broadcast_readback_cores(
    const SocDescriptor& soc_desc,
    const std::set<uint32_t>& rows_to_exclude,
    const std::set<uint32_t>& cols_to_exclude,
    const CoordSystem exclusion_coord_system) {
    std::set<uint32_t> target_rows;
    std::set<uint32_t> target_cols;
    std::map<std::pair<uint32_t, uint32_t>, CoreCoord> targets_by_row_and_col;
    for (const CoreCoord& core : soc_desc.get_cores(CoreType::TENSIX)) {
        const CoreCoord excluded_coord = soc_desc.translate_coord_to(core, exclusion_coord_system);
        if (rows_to_exclude.count(excluded_coord.y) > 0 || cols_to_exclude.count(excluded_coord.x) > 0) {
            continue;
        }
        target_rows.insert(excluded_coord.y);
        target_cols.insert(excluded_coord.x);
        targets_by_row_and_col.emplace(std::make_pair(excluded_coord.y, excluded_coord.x), core);
    }
    if (target_rows.empty() || target_cols.empty()) {
        return {};
    }

    const std::vector<uint32_t> rows(target_rows.begin(), target_rows.end());
    const std::vector<uint32_t> cols(target_cols.begin(), target_cols.end());
    std::vector<CoreCoord> sampled;
    for (size_t step = 0; step < std::max(rows.size(), cols.size()); step++) {
        // Harvesting can leave the target set non-rectangular, so a row/column pair may not exist.
        const auto target = targets_by_row_and_col.find({rows[step % rows.size()], cols[step % cols.size()]});
        if (target != targets_by_row_and_col.end()) {
            sampled.push_back(target->second);
        }
    }
    return sampled;
}

}  // namespace tt::umd::test_utils
