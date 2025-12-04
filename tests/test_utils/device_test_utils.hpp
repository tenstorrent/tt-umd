// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <fmt/ranges.h>

#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"

using namespace tt;
using namespace tt::umd;

namespace test_utils {

template <typename T>
static void size_buffer_to_capacity(std::vector<T>& data_buf, std::size_t size_in_bytes) {
    std::size_t target_size = 0;
    if (size_in_bytes > 0) {
        target_size = ((size_in_bytes - 1) / sizeof(T)) + 1;
    }
    data_buf.resize(target_size);
}

static void read_data_from_device(
    Cluster& cluster, std::vector<uint32_t>& vec, ChipId chip_id, CoreCoord core, uint64_t addr, uint32_t size) {
    size_buffer_to_capacity(vec, size);
    cluster.read_from_device(vec.data(), chip_id, core, addr, size);
}

inline void fill_with_random_bytes(uint8_t* data, size_t n) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    uint64_t* data64 = reinterpret_cast<uint64_t*>(data);
    std::generate_n(data64, n / 8, [&]() { return gen(); });

    // Handle remaining bytes
    for (size_t i = (n / 8) * 8; i < n; ++i) {
        data[i] = static_cast<uint8_t>(gen());
    }
}

inline std::string convert_to_comma_separated_string(const std::unordered_set<int>& devices) {
    return fmt::format("{}", fmt::join(devices, ","));
}

inline bool is_iommu_available() { return Cluster().get_tt_device(0)->get_pci_device()->is_iommu_enabled(); }

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

}  // namespace test_utils
