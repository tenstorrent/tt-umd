// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <utility>

// #include "host_api.hpp"
// #include "tt_metal.hpp"
#include "umd/device/lite_fabric/lite_fabric.h"

// #include "kernel_types.hpp"
// #include "program.hpp"
// #include "rtoptions.hpp"
// #include "tt_cluster.hpp"
// #include "assert.hpp"
// #include "context/metal_context.hpp"
// #include "jit_build/build_env_manager.hpp"
// #include "impl/kernels/kernel_impl.hpp"
// #include "core_coord.hpp"
// #include "data_types.hpp"
#include "umd/device/types/xy_pair.h"

namespace tt::umd {

namespace lite_fabric {

// Depth of 1
struct TunnelDescriptor {
    chip_id_t mmio_id{1};
    tt_xy_pair mmio_core_virtual{1, 1};
    tt_xy_pair mmio_core_logical{1, 1};
    // tt::tt_metal::KernelHandle mmio_kernel = 0;

    chip_id_t connected_id{1};
    tt_xy_pair connected_core_virtual{1, 1};
    tt_xy_pair connected_core_logical{1, 1};

    tt_cxy_pair mmio_cxy_virtual() const { return {(size_t)mmio_id, mmio_core_virtual}; }

    tt_cxy_pair connected_cxy_virtual() const { return {(size_t)connected_id, connected_core_virtual}; }
};

struct SystemDescriptor {
    std::map<chip_id_t, uint32_t> enabled_eth_channels;
    std::vector<TunnelDescriptor> tunnels_from_mmio;
};

static uint32_t get_eth_channel_mask(chip_id_t device_id);

static SystemDescriptor get_system_descriptor_2_devices(chip_id_t mmio_device_id, chip_id_t connected_device_id);

uint32_t get_local_init_addr();

void set_reset_state(Chip* chip, tt_cxy_pair virtual_core, bool assert_reset);

void set_reset_state(Chip* chip, const SystemDescriptor& desc, bool assert_reset);

void set_pc(Chip* chip, tt_cxy_pair virtual_core, uint32_t pc_addr, uint32_t pc_val);

void set_pc(Chip* chip, const SystemDescriptor& desc, uint32_t pc_addr, uint32_t pc_val);

void wait_for_state(Chip* chip, tt_cxy_pair virtual_core, uint32_t addr, lite_fabric::InitState state);

void wait_for_state(Chip* chip, const SystemDescriptor& desc, uint32_t addr, lite_fabric::InitState state);

void launch_lite_fabric(Chip* chip, const SystemDescriptor& desc, const std::filesystem::path& elf_path);

void launch_lite_fabric(Chip* chip, const SystemDescriptor& desc);

void terminate_lite_fabric(Chip* chip, const SystemDescriptor& desc);

}  // namespace lite_fabric

}  // namespace tt::umd
