#include "gtest/gtest.h"
#include "device/tt_soc_descriptor.h"
#include "device/tt_device.h"
#include "device/tt_emulation_device.h"

TEST(EmulationDeviceGS, BasicEmuTest) {
    tt_emulation_device device = tt_emulation_device("../../tests/soc_descs/grayskull_10x12.yaml");
    tt_device_params default_params;

    std::size_t phys_x = 1;
    std::size_t phys_y = 0;
    tt_xy_pair core = tt_xy_pair(phys_x, phys_y);

    uint32_t size = 10240;
    uint64_t l1_addr = 0x30000000;
                       
    std::vector<uint32_t> wdata(size);
    std::vector<uint32_t> rdata(size);
    
    try {
        device.start_device(default_params);

        for (auto &byte : wdata) {
            byte = rand();
        }
        std::vector<uint64_t> incorrect_addresses = {};
        for (uint64_t addr = 0xC00000; addr < 0x1000000; addr += 10240) {
            rdata = {};
            device.write_to_device(wdata, tt_cxy_pair(0, core), addr, "l1");
            device.read_from_device(rdata, tt_cxy_pair(0, core), addr, size * 4, "l1");
            if (rdata != wdata) {
                incorrect_addresses.push_back(addr);
            }
            if (incorrect_addresses.size() > 50) {
                break;
            }
        }
        std::cout << "Addresses with failed writes:" << std::endl;

        for (const auto& addr : incorrect_addresses) {
            std::cout << std::hex << addr << std::endl;
        }
        exit(0);
        // std::cout << "Writing vec" << std::endl;
        // device.write_to_device(wdata, tt_cxy_pair(0, core), l1_addr, "l1");
        // // std::cout << "Reading vec" << std::endl;
        // device.read_from_device(rdata, tt_cxy_pair(0, core), l1_addr, size * 4, "l1");
        // for (int i = 0; i < wdata.size(); i++) {
        //     std::cout << wdata.at(i) << " " << rdata.at(i) << std::endl;
        // }
        // ASSERT_EQ(wdata, rdata) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";

        // device.deassert_risc_reset();
        // std::cout << "Writing vec" << std::endl;
        // device.write_to_device(wdata, tt_cxy_pair(0, tt_xy_pair(phys_x, phys_y)), l1_addr, "l1");
        // std::cout << "Reading vec" <<std::endl;
        // device.read_from_device(rdata, tt_cxy_pair(0, core), l1_addr, size * 4, "l1");
        // device.assert_risc_reset();
        //  std::cout << "Writing vec" << std::endl;
        // device.write_to_device(wdata, tt_cxy_pair(0, tt_xy_pair(phys_x, phys_y)), l1_addr, "l1");
        // std::cout << "Reading vec" <<std::endl;
        // device.read_from_device(rdata, tt_cxy_pair(0, core), l1_addr, size * 4, "l1");
        
    } catch (const std::exception &e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    device.close_device();
}
