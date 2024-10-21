#include "blackhole_coordinate_manager.h"

std::set<std::size_t> BlackholeCoordinateManager::get_x_coordinates_to_harvest(std::size_t harvesting_mask) {
    std::set<std::size_t> x_to_harvest;
    std::size_t logical_x = 0;
    while (harvesting_mask > 0) {
        if (harvesting_mask & 1) {
            x_to_harvest.insert(logical_x);
        }
        logical_x++;
        harvesting_mask >>= 1;
    }
    return x_to_harvest;
}
