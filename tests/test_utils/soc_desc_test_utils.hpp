#pragma once

#include <cstdint>
#include <vector>
#include <string>


namespace test_utils {

static std::size_t get_num_harvested(std::size_t harvesting_mask) {
    std::size_t num_harvested = 0;
    while (harvesting_mask > 0) {
        if (harvesting_mask & 1) {
            num_harvested++;
        }
        harvesting_mask >>= 1;
    }
    return num_harvested;
}

}
