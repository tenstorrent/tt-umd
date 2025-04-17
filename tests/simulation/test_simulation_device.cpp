// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <random>

#include "device_fixture.hpp"
#include "tests/test_utils/device_test_utils.hpp"

std::vector<uint32_t> generate_data(uint32_t size_in_bytes) {
    size_t size = size_in_bytes / sizeof(uint32_t);
    std::vector<uint32_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 100);

    for (uint32_t i = 0; i < size; i++) {
        data[i] = dis(gen);
    }
    return data;
}

class LoopbackAllCoresParam : public SimulationDeviceFixture,
                              public ::testing::WithParamInterface<tt::umd::CoreCoord> {};

INSTANTIATE_TEST_SUITE_P(
    LoopbackAllCores,
    LoopbackAllCoresParam,
    ::testing::Values(
        tt::umd::CoreCoord{0, 1, CoreType::TENSIX, CoordSystem::VIRTUAL},
        tt::umd::CoreCoord{1, 1, CoreType::TENSIX, CoordSystem::VIRTUAL},
        tt::umd::CoreCoord{1, 0, CoreType::DRAM, CoordSystem::VIRTUAL}));

TEST_P(LoopbackAllCoresParam, LoopbackSingleTensix) {
    std::vector<uint32_t> wdata = {1, 2, 3, 4, 5};
    std::vector<uint32_t> rdata(wdata.size(), 0);
    tt::umd::CoreCoord core = GetParam();

    device->write_to_device(wdata.data(), wdata.size() * sizeof(uint32_t), 0, core, 0x100);
    device->read_from_device(rdata.data(), 0, core, 0x100, rdata.size() * sizeof(uint32_t));

    ASSERT_EQ(wdata, rdata);
}

bool loopback_stress_size(std::unique_ptr<tt_SimulationDevice> &device, tt::umd::CoreCoord core, uint32_t byte_shift) {
    uint64_t addr = 0x0;

    std::vector<uint32_t> wdata = generate_data(1 << byte_shift);
    std::vector<uint32_t> rdata(wdata.size(), 0);

    device->write_to_device(wdata.data(), wdata.size() * sizeof(uint32_t), 0, core, addr);
    device->read_from_device(rdata.data(), 0, core, addr, rdata.size() * sizeof(uint32_t));

    return wdata == rdata;
}

TEST_P(LoopbackAllCoresParam, LoopbackStressSize) {
    tt::umd::CoreCoord core = GetParam();
    tt::umd::CoreCoord dram = {1, 0, CoreType::DRAM, CoordSystem::VIRTUAL};
    if (core == dram) {
        for (uint32_t i = 2; i <= 30; ++i) {  // 2^30 = 1 GB
            ASSERT_TRUE(loopback_stress_size(device, core, i));
        }
    } else {
        for (uint32_t i = 2; i <= 20; ++i) {  // 2^20 = 1 MB
            ASSERT_TRUE(loopback_stress_size(device, core, i));
        }
    }
}

TEST_F(SimulationDeviceFixture, LoopbackTwoTensix) {
    std::vector<uint32_t> wdata1 = {1, 2, 3, 4, 5};
    std::vector<uint32_t> wdata2 = {6, 7, 8, 9, 10};
    std::vector<uint32_t> rdata1(wdata1.size());
    std::vector<uint32_t> rdata2(wdata2.size());
    tt::umd::CoreCoord core1 = {0, 1, CoreType::TENSIX, CoordSystem::VIRTUAL};
    tt::umd::CoreCoord core2 = {1, 1, CoreType::TENSIX, CoordSystem::VIRTUAL};

    device->write_to_device(wdata1.data(), wdata1.size() * sizeof(uint32_t), 0, core1, 0x100);
    device->write_to_device(wdata2.data(), wdata2.size() * sizeof(uint32_t), 0, core2, 0x100);

    device->read_from_device(rdata1.data(), 0, core1, 0x100, rdata1.size() * sizeof(uint32_t));
    device->read_from_device(rdata2.data(), 0, core2, 0x100, rdata2.size() * sizeof(uint32_t));

    ASSERT_EQ(wdata1, rdata1);
    ASSERT_EQ(wdata2, rdata2);
}
