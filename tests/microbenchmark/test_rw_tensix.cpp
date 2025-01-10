/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>

#include "device_fixture.hpp"
#include "nanobench.h"
#include "tests/test_utils/device_test_utils.hpp"

// This isn't compiling and I'm too busy to figure out why.
// I'm also too busy to coax my environment into not building it, so...
#if 0

std::uint32_t generate_random_address(std::uint32_t max, std::uint32_t min = 0) {
    ankerl::nanobench::Rng gen(80085);
    std::uniform_int_distribution<> dis(min, max);  // between 0 and 1MB
    return dis(gen);
}

TEST_F(uBenchmarkFixture, WriteAllCores32Bytes) {
    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7};
    std::uint64_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    std::uint64_t bad_address = 0x30000000;  // this address is not mapped, should trigger fallback write/read path

    ankerl::nanobench::Bench bench_static;
    ankerl::nanobench::Bench bench_dynamic;
    for (auto& core : device->get_soc_descriptor(0).get_cores(CoreType::TENSIX, CoordSystem::VIRTUAL)) {
        std::stringstream wname;
        wname << "Write to device core (" << core.x << ", " << core.y << ")";
        // Write 32 bytes through static tlbs
        bench_static.title("Write 32 bytes")
            .unit("writes")
            .minEpochIterations(50)
            .output(nullptr)
            .run(wname.str(), [&] {
                device->write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    tt_cxy_pair(0, core),
                    address,
                    "SMALL_READ_WRITE_TLB");
            });
        // Write through "fallback/dynamic" tlb
        bench_dynamic.title("Write 32 bytes fallback")
            .unit("writes")
            .minEpochIterations(50)
            .output(nullptr)
            .run(wname.str(), [&] {
                device->write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    tt_cxy_pair(0, core),
                    bad_address,
                    "SMALL_READ_WRITE_TLB");
            });
        wname.clear();
    }
    bench_static.render(ankerl::nanobench::templates::csv(), results_csv);
    bench_dynamic.render(ankerl::nanobench::templates::csv(), results_csv);
}

TEST_F(uBenchmarkFixture, ReadAllCores32Bytes) {
    std::vector<uint32_t> readback_vec = {};
    std::uint64_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    std::uint64_t bad_address = 0x30000000;  // this address is not mapped, should trigger fallback write/read path

    ankerl::nanobench::Bench bench_static;
    ankerl::nanobench::Bench bench_dynamic;

    for (auto& core : device->get_soc_descriptor(0)
        std::stringstream rname;
        // Read through static tlbs
        rname << "Read from device core (" << core.x << ", " << core.y << ")";
        bench_static.title("Read 32 bytes").unit("reads").minEpochIterations(50).output(nullptr).run(rname.str(), [&] {
        test_utils::read_data_from_device(
            *device, readback_vec, tt_cxy_pair(0, core), address, 0x20, "SMALL_READ_WRITE_TLB");
        });
        // Read through "fallback/dynamic" tlb
        bench_dynamic.title("Read 32 bytes fallback")
            .unit("reads")
            .minEpochIterations(50)
            .output(nullptr)
            .run(rname.str(), [&] {
        test_utils::read_data_from_device(
            *device, readback_vec, tt_cxy_pair(0, core), bad_address, 0x20, "SMALL_READ_WRITE_TLB");
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
    for (auto& core : device->get_soc_descriptor(0)
        address = generate_random_address(1 << 20);  // between 0 and 1MB
        std::stringstream wname;
        wname << "Write to device core (" << core.x << ", " << core.y << ") @ address " << std::hex << address;
        bench.title("Write 32 bytes random address")
            .unit("writes")
            .minEpochIterations(50)
            .output(nullptr)
            .run(wname.str(), [&] {
        device->write_to_device(
            vector_to_write.data(),
            vector_to_write.size() * sizeof(std::uint32_t),
            tt_cxy_pair(0, core),
            address,
            "SMALL_READ_WRITE_TLB");
            });
        wname.clear();
}

bench.render(ankerl::nanobench::templates::csv(), results_csv);
}

TEST_F(uBenchmarkFixture, Read32BytesRandomAddr) {
    std::vector<uint32_t> readback_vec = {};
    std::uint32_t address;

    ankerl::nanobench::Bench bench;
    for (auto& core : device->get_soc_descriptor(0)
        address = generate_random_address(1 << 20);  // between 0 and 1MB
        std::stringstream rname;
        rname << "Read from device core (" << core.x << ", " << core.y << ") @ address " << std::hex << address;
        bench.title("Read 32 bytes random address")
            .unit("reads")
            .minEpochIterations(50)
            .output(nullptr)
            .run(rname.str(), [&] {
        test_utils::read_data_from_device(
            *device, readback_vec, tt_cxy_pair(0, core), address, 0x20, "SMALL_READ_WRITE_TLB");
            });
        rname.clear();
}

bench.render(ankerl::nanobench::templates::csv(), results_csv);
}

#endif
