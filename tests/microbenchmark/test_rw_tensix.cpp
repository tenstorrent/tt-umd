/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>

#include "device_fixture.hpp"
#include "nanobench.h"
#include "tests/test_utils/device_test_utils.hpp"

std::uint32_t generate_random_address(std::uint32_t max, std::uint32_t min = 0) {
    ankerl::nanobench::Rng gen(80085);
    std::uniform_int_distribution<> dis(min, max);  // between 0 and 1MB
    return dis(gen);
}

TEST_F(uBenchmarkFixture, WriteAllCores32Bytes) {
    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7};
    std::uint64_t address = 0x100;
    std::uint64_t bad_address = 0x30000000;  // this address is not mapped, should trigger fallback write/read path

    ankerl::nanobench::Bench bench_static;
    ankerl::nanobench::Bench bench_dynamic;
    for (auto& core_coord : device->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        std::stringstream wname;
        wname << "Write to device core (" << core_coord.x << ", " << core_coord.y << ")";
        // Write through "fallback/dynamic" tlb
        bench_dynamic.title("Write 32 bytes fallback")
            .unit("writes")
            .minEpochIterations(50)
            .output(nullptr)
            .run(wname.str(), [&] {
                device->write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), 0, core_coord, bad_address);
            });
        wname.clear();
    }
    bench_static.render(ankerl::nanobench::templates::csv(), results_csv);
    bench_dynamic.render(ankerl::nanobench::templates::csv(), results_csv);
}

TEST_F(uBenchmarkFixture, ReadAllCores32Bytes) {
    std::vector<uint32_t> readback_vec = {};
    std::uint64_t address = 0x100;
    std::uint64_t bad_address = 0x30000000;  // this address is not mapped, should trigger fallback write/read path

    ankerl::nanobench::Bench bench_static;
    ankerl::nanobench::Bench bench_dynamic;

    for (auto& core_coord : device->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        std::stringstream rname;
        // Read through "fallback/dynamic" tlb
        bench_dynamic.title("Read 32 bytes fallback")
            .unit("reads")
            .minEpochIterations(50)
            .output(nullptr)
            .run(rname.str(), [&] {
                test_utils::read_data_from_device(*device, readback_vec, 0, core_coord, bad_address, 0x20);
            });
        rname.clear();
    }

    bench_static.render(ankerl::nanobench::templates::csv(), results_csv);
    bench_dynamic.render(ankerl::nanobench::templates::csv(), results_csv);
}

TEST_F(uBenchmarkFixture, Write32BytesRandomAddr) {
    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7};
    std::uint32_t address;

    ankerl::nanobench::Bench bench;
    for (auto& core_coord : device->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        address = generate_random_address(1 << 20);  // between 0 and 1MB
        std::stringstream wname;
        wname << "Write to device core (" << core_coord.x << ", " << core_coord.y << ") @ address " << std::hex
              << address;
        bench.title("Write 32 bytes random address")
            .unit("writes")
            .minEpochIterations(50)
            .output(nullptr)
            .run(wname.str(), [&] {
                device->write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), 0, core_coord, address);
            });
        wname.clear();
    }

    bench.render(ankerl::nanobench::templates::csv(), results_csv);
}

TEST_F(uBenchmarkFixture, Read32BytesRandomAddr) {
    std::vector<uint32_t> readback_vec = {};
    std::uint32_t address;

    ankerl::nanobench::Bench bench;
    for (auto& core_coord : device->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        address = generate_random_address(1 << 20);  // between 0 and 1MB
        std::stringstream rname;
        rname << "Read from device core (" << core_coord.x << ", " << core_coord.y << ") @ address " << std::hex
              << address;
        bench.title("Read 32 bytes random address")
            .unit("reads")
            .minEpochIterations(50)
            .output(nullptr)
            .run(rname.str(), [&] {
                test_utils::read_data_from_device(*device, readback_vec, 0, core_coord, address, 0x20);
            });
        rname.clear();
    }

    bench.render(ankerl::nanobench::templates::csv(), results_csv);
}
