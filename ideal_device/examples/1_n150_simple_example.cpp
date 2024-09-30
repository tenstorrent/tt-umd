#include "cluster/cluster.h"
#include "chip/soc_descriptor.h"
#include "core/core.h"

int main() {

    {
        // example1: create using cluster
        tt::umd::Cluster cluster;
        tt::umd::Chip *chip = cluster.get_chip(0);

        // example2: create using chip
        tt::umd::Chip *chip = new tt::umd::Chip(0);

        std::unordered_set<tt::umd::physical_coord> all_worker_cores = cluster.get_chip(0)->get_soc_descriptor()->physical_workers;
        // Write some firmware to all worker cores
        chip->run_on_cores([&](tt::umd::Core* core) {
            std::vector<uint32_t> some_random_firmware;
            core->write_to_device(some_random_firmware.data(), some_random_firmware.size(), 0);
        }, all_worker_cores);

        // example1: Write something to dram at address 0
        std::vector<uint32_t> some_data;
        chip->get_dram_core(0)->write_to_device(some_data.data(), some_data.size(), 0);
        // example2: Write something to dram at address 0
        chip->get_core(chip->get_soc_descriptor()->get_physical_from_logical_dram(0, 0))->write_to_device(some_data.data(), some_data.size(), 0);
        // example3: Write something to dram, where some structure starts at address 10
        std::unique_ptr<tt::umd::AbstractIO> io = chip->get_dram_core(0)->get_io(10);
        io->write(some_data.data(), some_data.size(), 0);
        // example4: Write something to dram
        cluster->get_chip(0)->get_dram_core(0)->write_to_device(some_data.data(), some_data.size(), 0);

        // example1: Write something to sysmem
        chip->write_to_sysmem(some_data.data(), some_data.size(), 0);
        // example2: Write something to sysmem
        std::unique_ptr<tt::umd::AbstractIO> sysmem_io = chip->get_sysmem_io(0);
        sysmem_io->write(some_data.data(), some_data.size(), 0);

        // Start cluster
        cluster.start_cluster({});
        // Or start a single chip
        chip->start_device({});

        // test eth version
        tt::umd::tt_version some_fw_version = 0x12345678;
        assert(cluster.get_ethernet_fw_version() > some_fw_version);


    }

}