// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstring>
#include <iostream>
#include <random>

#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"

using namespace tt;
using namespace tt::umd;

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --start-device       Start the device before testing\n"
              << "  --help, -h           Show this help message\n";
}

void fill_with_random_data(void* ptr, size_t bytes) {
    if (bytes == 0) {
        return;
    }
    static std::mt19937_64 eng(0xCAFEF00D);  // Use fixed seed for reproducibility
    size_t num_uint64 = bytes / sizeof(uint64_t);
    size_t rem_bytes = bytes % sizeof(uint64_t);
    uint64_t* ptr64 = static_cast<uint64_t*>(ptr);
    for (size_t i = 0; i < num_uint64; ++i) {
        ptr64[i] = eng();
    }
    if (rem_bytes > 0) {
        uint8_t* ptr8 = reinterpret_cast<uint8_t*>(ptr64 + num_uint64);
        uint64_t last_chunk = eng();
        memcpy(ptr8, &last_chunk, rem_bytes);
    }
}

int main(int argc, char* argv[]) {
    bool start_device = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--start-device") == 0) {
            start_device = true;
        } else {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    Cluster cluster;
    auto descriptor = cluster.get_cluster_description();
    auto arch = descriptor->get_arch();

    if (arch != tt::ARCH::WORMHOLE_B0) {
        std::cerr << "Must be Wormhole" << std::endl;
        return 1;
    }

    if (start_device) {
        cluster.start_device({});
    }

    auto local_ids = cluster.get_target_mmio_device_ids();
    auto remote_ids = cluster.get_target_remote_device_ids();

    if (remote_ids.empty()) {
        std::cerr << "Need a remote chip" << std::endl;
        return 1;
    }

    std::vector<uint8_t> buffer(1 << 21, 0x00);
    std::vector<uint8_t> readback(1 << 21, 0x00);
    fill_with_random_data(&buffer[0], buffer.size());

    auto chip_id = *remote_ids.begin();
    CoreCoord core(0, 0, CoreType::DRAM, CoordSystem::NOC0);
    uint64_t address = 0;

    cluster.write_to_device(buffer.data(), buffer.size(), chip_id, core, address);
    cluster.wait_for_non_mmio_flush(chip_id);
    cluster.read_from_device(readback.data(), chip_id, core, address, buffer.size());

    if (buffer != readback) {
        std::cerr << "Buffer and readback do not match" << std::endl;
        return 1;
    }

    return 0;
}
