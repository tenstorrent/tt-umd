#include "cluster/cluster.h"
#include "chip/local_chip.h"
#include "chip/soc_descriptor.h"
#include "core/core.h"
#include "tt_device/tt_device.h"

int main() {

    {
        // Use through cluster interface
        tt::umd::Cluster cluster;

        tt::umd::Chip *chip = cluster.get_chip(0);

        assert(chip->get_chip_type() == tt::umd::ChipType::Local);
        tt::umd::LocalChip local_chip = dynamic_cast<tt::umd::LocalChip&>(*chip);

        tt::umd::TTDevice *tt_device = local_chip->tt_device.get();

        std::unordered_map<tt::umd::tlb_index, tt::umd::physical_coord> tlb_map;

        tt_device->set_dynamic_tlb(0, tt::umd::physical_coord{1, 1}, 0x0);
        tlb_map[tt::umd::tlb_index{tt::umd::tlb_type::tlb_2m, 0}] = tt::umd::physical_coord{1, 1};

        local_chip->setup_core_to_tlb_map(tlb_map);

    }

}