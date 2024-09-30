#include "gtest/gtest.h"
#include "new_device/soc_descriptor.h"
#include "new_device/tt_device.h"
#include "new_device/tt_emulation_device.h"

// DEPRECATED TEST SUITE !!!

TEST(EmulationDeviceGS, BasicEmuTest) {
    tt_emulation_device device = tt_emulation_device("../../tests/soc_descs/grayskull_10x12.yaml");
    device_params default_params;

    std::size_t phys_x = 1;
    std::size_t phys_y = 1;
    xy_pair core = xy_pair(phys_x, phys_y);

    uint32_t size = 16;
    uint64_t l1_addr = 0x1000;
    std::vector<uint32_t> wdata(size);
    std::vector<uint32_t> rdata(size);
    
    try {
        device.start_device(default_params);

        for (auto &byte : wdata) {
            byte = rand();
        }
        device.write_to_device(wdata, cxy_pair(0, core), l1_addr, "l1");
        device.read_from_device(rdata, cxy_pair(0, core), l1_addr, size, "l1");
        ASSERT_EQ(wdata, rdata) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";

        device.deassert_risc_reset();
        device.write_to_device(wdata, cxy_pair(0, xy_pair(phys_x, phys_y)), l1_addr, "l1");
        device.assert_risc_reset();
        device.write_to_device(wdata, cxy_pair(0, xy_pair(phys_x, phys_y)), l1_addr, "l1");


    } catch (const std::exception &e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    device.close_device();
}
