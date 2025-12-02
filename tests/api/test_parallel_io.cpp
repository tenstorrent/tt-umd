/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>

#include "umd/device/cluster.hpp"
#include "umd/device/pcie/parallel_io.hpp"

using namespace tt;
using namespace tt::umd;

TEST(TestParallelIO, Basic) {
    const uint64_t dram_addr = 0;
    // const tt_xy_pair dram_core = {1, 2};
    const CoreCoord dram_core = CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0);
    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    uint32_t val = 0;
    // for (CoreCoord core : tensix_cores) {
    //     cluster->write_to_device(&val, sizeof(uint32_t), chip, core, tensix_addr);
    //     val++;
    // }

    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device().get();

    const uint64_t bytes = 4ULL << 30;

    std::unique_ptr<ParallelIO> parallel_io =
        std::make_unique<ParallelIO>(64, dram_core, dram_addr, bytes, pci_device->pci_device_file_desc);

    uint8_t write_data = 0x12;
    std::vector<uint8_t> write_buffer(bytes, write_data);

    // Add measurement of time taken for write in nanoseconds
    {
        auto start = std::chrono::high_resolution_clock::now();
       
        parallel_io->write_to_device(write_buffer.data());
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;

        double seconds = elapsed.count();
        double bw_gb_s = (bytes / seconds) / (1024.0 * 1024.0 * 1024.0);  // GB/s (GiB/s)

        std::cout << "Transferred " << bytes << " bytes\n";
        std::cout << "Time: " << seconds << " s\n";
        std::cout << "Bandwidth: " << bw_gb_s << " GB/s\n";
    }

    // {
    //     auto start = std::chrono::high_resolution_clock::now();
    //     cluster->write_to_device(write_buffer.data(), bytes, 0, dram_core, 0);
    //     auto end = std::chrono::high_resolution_clock::now();
    //     std::chrono::duration<double> elapsed = end - start;

    //     double seconds = elapsed.count();
    //     double bw_gb_s = (bytes / seconds) / (1024.0 * 1024.0 * 1024.0);  // GB/s (GiB/s)

    //     std::cout << "Transferred " << bytes << " bytes\n";
    //     std::cout << "Time: " << seconds << " s\n";
    //     std::cout << "Bandwidth: " << bw_gb_s << " GB/s\n";
    // }

    // {
    //     auto start = std::chrono::high_resolution_clock::now();
    //     std::vector<uint8_t> read_buffer(bytes, write_data - 1);
    //     parallel_io->read_from_device(read_buffer.data());
    //     auto end = std::chrono::high_resolution_clock::now();
    //     std::chrono::duration<double> elapsed = end - start;

    //     double seconds = elapsed.count();
    //     double bw_gb_s = (bytes / seconds) / (1024.0 * 1024.0 * 1024.0);  // GB/s (GiB/s)

    //     std::cout << "Transferred " << bytes << " bytes\n";
    //     std::cout << "Time: " << seconds << " s\n";
    //     std::cout << "Bandwidth: " << bw_gb_s << " GB/s\n";

    //     for (size_t i = 0; i < bytes; ++i) {
    //         if (read_buffer[i] != write_data) {
    //             std::cout << "Mismatch at index " << i << ": " << (uint32_t)read_buffer[i] << std::endl;
    //             ASSERT_TRUE(false);
    //         }
    //         // ASSERT_EQ(read_buffer[i], 0xBC);
    //     }
    // }

    // {
    //     auto start = std::chrono::high_resolution_clock::now();
    //     std::vector<uint8_t> read_buffer(bytes, write_data - 1);
    //     // parallel_io->read_from_device(read_buffer.data());
    //     //   void read_from_device(void* mem_ptr, ChipId chip, CoreCoord core, uint64_t addr, uint32_t size);
    //     cluster->read_from_device(read_buffer.data(), 0, dram_core, 0, bytes);
    //     auto end = std::chrono::high_resolution_clock::now();
    //     std::chrono::duration<double> elapsed = end - start;

    //     double seconds = elapsed.count();
    //     double bw_gb_s = (bytes / seconds) / (1024.0 * 1024.0 * 1024.0);  // GB/s (GiB/s)

    //     std::cout << "Transferred " << bytes << " bytes\n";
    //     std::cout << "Time: " << seconds << " s\n";
    //     std::cout << "Bandwidth: " << bw_gb_s << " GB/s\n";
    // }
}
