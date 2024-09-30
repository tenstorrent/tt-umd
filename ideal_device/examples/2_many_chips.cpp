#include "cluster/cluster.h"
#include "io/abstract_io.h"

int main(){

    tt::umd::ClusterDescriptor *cluster_descriptor = tt::umd::ClusterDescriptor::get_cluster_descriptor();
    
    // Obtain some group of chips connected to a single PCI
    tt::umd::chip_id_t any_mmio_chip = cluster_descriptor->get_mmio_chips().begin()->first;
    std::unordered_set<tt::umd::chip_id_t> single_pci_chips;
    for (std::pair<tt::umd::chip_id_t, tt::umd::chip_id_t> chip_to_mmio : cluster_descriptor->closest_mmio_chip_cache() ) {
        if (chip_to_mmio.second == any_mmio_chip) {
            single_pci_chips.insert(chip_to_mmio.first);
        }
    }

    // open only a group of chips, not whole cluster.
    tt::umd::Cluster cluster(single_pci_chips);

    // The contination of example is the same whether whole cluster or part of it is used.

    std::unordered_set<tt::umd::chip_id_t> ethernet_cores_used_on_all_chips = {any_mmio_chip->get_soc_descriptor()->get_ethernet_cores()[0]};
    // Set used ethernet cores for remote chips.
    cluster.run_on_chips([&](tt::umd::Chip* chip) { 
        chip->configure_active_ethernet_cores_for_mmio_device(ethernet_cores_used_on_all_chips);
    });

    // Run same firmware on all chips.
    cluster.run_on_chips([&](tt::umd::Chip* chip) { 
        
        std::vector<uint32_t> some_random_firmware;
        // Write to all worker cores.
        chip->run_on_cores([&](tt::umd::Core* core) { 
            core->write_to_device(some_random_firmware.data(), some_random_firmware.size(), 0); 
        }, chip->get_soc_descriptor().get_all_worker_cores());

        chip->deassert_risc_reset();
    });

    // Membar all remote chips individually.
    cluster.run_on_chips([&](tt::umd::Chip* chip) { 
        chip->wait_for_non_mmio_flush();
    });

    // l1_membar on all worker cores and dram_membar on all dram cores.
    cluster.run_on_chips([&](tt::umd::Chip* chip) { 
        chip->run_on_cores([&](tt::umd::Core* core) { core->l1_membar(); }, chip->get_soc_descriptor().get_all_worker_cores());
        chip->run_on_cores([&](tt::umd::Core* core) { core->dram_membar(); }, chip->get_soc_descriptor().get_all_dram_cores());
    });

    // get mapping of all clocks
    std::unordered_map<chip_id_t, int> clocks = cluster.get_clocks();

    // get num node for a specific chip.
    int numanode = cluster.get_chip(0)->get_numa_node();

    // write to host channels if they exist 
    for (tt::umd::Chip* chip : cluster->get_chips()) {
        if (chip->get_num_host_channels() > 0 && chip->get_host_channel_size(0) > 1000000) {
            chip->write_to_sysmem(some_data.data(), some_data.size(), 0);
        }
    }

    // Get some core writer.
    // This could be a DRAM or worker tensix.
    // This could be a Local or Remote core.
    // If this is a Local core with static TLB, the write will be very fast.
    std::unique_ptr<tt::umd::AbstractIO> core_io = cluster.get_chip(0)->get_core(0, 0)->get_io();
    core_io->write_u32(0, 0x12345678);

}