#include "device/tt_soc_descriptor.h"
#include "device/tt_device.h"
#include "device/tt_vcs_device.h"


int axi_rw_test() {
    std::set<chip_id_t> target_devices = {0, 1};

    tt_vcs_device device = tt_vcs_device("./tests/soc_descs/grayskull_10x12.yaml");
    tt_device_params default_params;

    int err = 0;
    std::size_t phys_x = 1;
    std::size_t phys_y = 0;
    uint32_t size = 300;
    uint64_t l1_addr = 0x1000;
    std::vector<uint32_t> wdata(size);
    std::vector<uint32_t> rdata(size);

    device.start_device(default_params);
    // device.clean_system_resources();
    try {
        for (auto &byte : wdata) {
            byte = rand();
        }
        device.write_to_device(wdata, tt_cxy_pair(0, tt_xy_pair(phys_x, phys_y)), l1_addr, "l1");

        device.read_from_device(rdata, tt_cxy_pair(0, tt_xy_pair(phys_x, phys_y)), l1_addr, size, "l1");
        
        for (u_int32_t i = 0; i < size; i++) {
            if (wdata.at(i) != rdata.at(i)) {
                // print_with_path("Error wdata[%d](0x%x) != rdata[%d](0x%x)", i, wdata.at(i), i, rdata.at(i));
                err++;
            } 
            // else if (i < 5 || i > size - 5) {
                // print_with_path("wdata[%d] = 0x%x | rdata[%d] = 0x%x ", i, wdata.at(i), i, rdata.at(i));

            // } else if (i == 5) {
            //     print_with_path("...");
            // }
        }

        // if (err) {
            // print_with_path("Error: %d errors", err);
            // sv_error();
        // }

        // print_with_path("--------------------------------------------------");
        return err;

    } catch (const std::exception &e) {
        // print_with_path("Error: %s", e.what());
        return 1;
    }

    for (int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    return err;
}
