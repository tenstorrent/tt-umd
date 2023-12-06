#pragma once

#include <cstdint>
#include <cassert>

#include "tt_xy_pair.h"

namespace tt::umd::test::utils {

using address_t = uint32_t;
using destination_t = tt_cxy_pair;
using transfer_size_t = uint32_t;


struct transfer_type_weights_t {
    double write;
    double rolled_write;
    double read;
    double epoch_cmd_write;
};


static auto address_aligner = [](address_t addr) -> address_t { addr = (((addr - 1) / 32) + 1) * 32; assert(addr % 32 == 0); return addr;};
static auto transfer_size_aligner = [](transfer_size_t size) -> transfer_size_t { size = (((size - 1) / 4) + 1) * 4; assert(size > 0); assert(size % 4 == 0); return size; };
static auto rolled_write_transfer_size_aligner = [](transfer_size_t size) -> transfer_size_t { size = (((size - 1) / 32) + 1) * 32; assert(size > 0); return size;};
static auto address_aligner_32B = [](transfer_size_t size) -> transfer_size_t { size = (((size - 1) / 32) + 1) * 32; assert(size > 0); return size;};
static auto size_aligner_32B = [](transfer_size_t size) -> transfer_size_t { size = (((size - 1) / 32) + 1) * 32; assert(size > 0); return size;};
template<typename T>
static auto passthrough_constrainer = [](T const& t) -> T { return t; };

}; // namespace tt::umd::test::utils