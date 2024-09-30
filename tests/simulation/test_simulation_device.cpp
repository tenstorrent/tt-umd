// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <random>
#include "device_fixture.hpp"

std::vector<uint32_t> generate_data(uint32_t size_in_bytes){
    size_t size = size_in_bytes/sizeof(uint32_t);
    std::vector<uint32_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 100);

    for(uint32_t i = 0; i < size; i++){
        data[i] = dis(gen);
    }
    return data;
}

class LoopbackAllCoresParam : public SimulationDeviceFixture , 
                            public ::testing::WithParamInterface<xy_pair> {};

INSTANTIATE_TEST_SUITE_P(
    LoopbackAllCores,
    LoopbackAllCoresParam,
    ::testing::Values(
        xy_pair{0, 1},
        xy_pair{1, 1},
        xy_pair{1, 0}
    )
);

TEST_P(LoopbackAllCoresParam, LoopbackSingleTensix){
    std::vector<uint32_t> wdata = {1,2,3,4,5};
    std::vector<uint32_t> rdata(wdata.size(), 0);
    cxy_pair core = {0, GetParam()};

    device->write_to_device(wdata.data(), wdata.size()*sizeof(uint32_t), core, 0x100, "");
    device->read_from_device(rdata.data(), core, 0x100, wdata.size()*sizeof(uint32_t), "");
    
    ASSERT_EQ(wdata, rdata);
}

bool loopback_stress_size(std::unique_ptr<tt_SimulationDevice> &device, xy_pair core, uint32_t byte_shift){
    uint64_t addr = 0x0;

    std::vector<uint32_t> wdata = generate_data(1 << byte_shift);
    std::vector<uint32_t> rdata(wdata.size(), 0);

    device->write_to_device(wdata.data(), wdata.size()*sizeof(uint32_t), cxy_pair{0, core}, addr, "");
    device->read_from_device(rdata.data(), cxy_pair{0, core}, addr, wdata.size()*sizeof(uint32_t), "");
    
    return wdata == rdata;
}

TEST_P(LoopbackAllCoresParam, LoopbackStressSize){
    xy_pair core = GetParam();
    xy_pair dram = {1, 0};
    if (core == dram) {
        for (uint32_t i = 2; i <= 30; ++i) {    // 2^30 = 1 GB
            ASSERT_TRUE(loopback_stress_size(device, core, i));
        }
    } else {
        for (uint32_t i = 2; i <= 20; ++i) {    // 2^20 = 1 MB
            ASSERT_TRUE(loopback_stress_size(device, core, i));
        }
    }
}

TEST_F(SimulationDeviceFixture, LoopbackTwoTensix){
    std::vector<uint32_t> wdata1 = {1,2,3,4,5};
    std::vector<uint32_t> wdata2 = {6,7,8,9,10};
    std::vector<uint32_t> rdata1(wdata1.size());
    std::vector<uint32_t> rdata2(wdata2.size());
    cxy_pair core1 = {0, 0, 1};
    cxy_pair core2 = {0, 1, 1};

    device->write_to_device(wdata1.data(), wdata1.size()*sizeof(uint32_t),  core1, 0x100, "");
    device->write_to_device(wdata2.data(), wdata2.size()*sizeof(uint32_t), core2, 0x100, "");

    device->read_from_device(rdata1.data(), core1, 0x100, wdata1.size()*sizeof(uint32_t), "");
    device->read_from_device(rdata2.data(), core2, 0x100, wdata2.size()*sizeof(uint32_t), "");
    
    ASSERT_EQ(wdata1, rdata1);
    ASSERT_EQ(wdata2, rdata2);
}
