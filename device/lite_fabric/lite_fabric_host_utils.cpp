// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/lite_fabric/lite_fabric_host_utils.h"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip/chip.h"
#include "umd/device/lite_fabric/blackhole_dev_mem_map.h"
#include "umd/device/lite_fabric/lite_fabric.h"

namespace {
uint32_t get_state_address_metal() {
    // uint32_t state_addr =
    //     tt::tt_metal::MetalContext::instance().hal().get_dev_addr(
    //         tt::tt_metal::HalProgrammableCoreType::ACTIVE_ETH, tt::tt_metal::HalL1MemAddrType::FABRIC_LITE_CONFIG) +
    //     offsetof(lite_fabric::LiteFabricMemoryMap, config) + offsetof(lite_fabric::LiteFabricConfig, current_state);
    uint32_t state_addr = 0;
    return state_addr;
}

uint32_t get_state_address() {
    return MEM_LITE_FABRIC_CONFIG_BASE + offsetof(tt::umd::lite_fabric::LiteFabricConfig, current_state);
}
}  // namespace

namespace tt::umd {

namespace lite_fabric {

uint32_t get_eth_channel_mask(chip_id_t device_id) {
    // auto& cp = tt::tt_metal::MetalContext::instance().get_control_plane();

    // uint32_t mask = 0;
    // for (const auto& core : cp.get_active_ethernet_cores(device_id)) {
    //     mask |= 0x1 << core.y;
    // }

    uint32_t mask = 0;
    return mask;
}

SystemDescriptor get_system_descriptor_2_devices(chip_id_t mmio_device_id, chip_id_t connected_device_id) {
    SystemDescriptor desc;
    return desc;
}

uint32_t get_local_init_addr() {
    // TODO(pjanevski): Implement this
    return 0;
}

void set_reset_state(Chip* chip, tt_cxy_pair virtual_core, bool assert_reset) {
    // We run on DM1. Don't touch DM0. It is running base firmware
    TensixSoftResetOptions reset_val = TENSIX_ASSERT_SOFT_RESET;
    if (assert_reset) {
        reset_val = reset_val & static_cast<TensixSoftResetOptions>(
                                    ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::BRISC));

        // TODO(pjanevski): Implement this.
        // chip->assert_risc_reset_at_core(virtual_core, reset_val);
    } else {
        reset_val = TENSIX_DEASSERT_SOFT_RESET &
                    static_cast<TensixSoftResetOptions>(
                        ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::TRISC0));

        // TODO(pjanevski): Implement this.
        // chip->deassert_risc_reset_at_core(virtual_core, reset_val);
    }
}

void set_reset_state(Chip* chip, const SystemDescriptor& desc, bool assert_reset) {
    for (auto tunnel_1x : desc.tunnels_from_mmio) {
        set_reset_state(chip, tunnel_1x.mmio_cxy_virtual(), assert_reset);
    }
}

void set_pc(Chip* chip, tt_cxy_pair virtual_core, uint32_t pc_addr, uint32_t pc_val) {
    CoreCoord umd_core = CoreCoord(virtual_core.x, virtual_core.y, CoreType::ETH, CoordSystem::TRANSLATED);
    chip->write_to_device(umd_core, (void*)&pc_val, pc_addr, sizeof(uint32_t));
}

void set_pc(Chip* chip, const SystemDescriptor& desc, uint32_t pc_addr, uint32_t pc_val) {
    for (auto tunnel_1x : desc.tunnels_from_mmio) {
        set_pc(chip, tunnel_1x.mmio_cxy_virtual(), pc_addr, pc_val);
    }
}

void wait_for_state(Chip* chip, tt_cxy_pair virtual_core, uint32_t addr, InitState state) {
    std::vector<uint32_t> readback{static_cast<uint32_t>(lite_fabric::InitState::UNKNOWN)};
    while (static_cast<InitState>(readback[0]) != state) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const CoreCoord umd_core = CoreCoord(virtual_core.x, virtual_core.y, CoreType::ETH, CoordSystem::TRANSLATED);
        chip->read_from_device(umd_core, readback.data(), addr, sizeof(uint32_t));
    }
}

void wait_for_state(Chip* chip, const SystemDescriptor& desc, uint32_t addr, InitState state) {
    for (auto tunnel_1x : desc.tunnels_from_mmio) {
        wait_for_state(chip, tunnel_1x.mmio_cxy_virtual(), addr, state);
    }
}

void launch_lite_fabric(Chip* chip, const SystemDescriptor& desc, const std::filesystem::path& elf_path) {
    constexpr uint32_t k_FirmwareStart = MEM_LITE_FABRIC_FIRMWARE_BASE;
    constexpr uint32_t k_PcResetAddress = MEM_LITE_FABRIC_RESET_PC;

    LiteFabricConfig config{};
    config.is_primary = true;
    config.is_mmio = true;
    config.initial_state = InitState::ETH_INIT_NEIGHBOUR;
    config.current_state = InitState::ETH_INIT_NEIGHBOUR;
    config.binary_addr = 0;
    config.binary_size = 0;
    config.eth_chans_mask = desc.enabled_eth_channels.at(0);
    config.routing_enabled = true;

    // Need an abstraction layer for Lite Fabric
    auto config_addr = MEM_LITE_FABRIC_CONFIG_BASE;

    for (const auto& tunnel_1x : desc.tunnels_from_mmio) {
        set_reset_state(chip, tunnel_1x.mmio_cxy_virtual(), true);
        set_pc(chip, tunnel_1x.mmio_cxy_virtual(), k_PcResetAddress, k_FirmwareStart);

        // const ll_api::memory& bin = ll_api::memory(elf_path.string(), ll_api::memory::Loading::DISCRETE);

        // auto local_init = MEM_LITE_FABRIC_INIT_LOCAL_L1_BASE_SCRATCH;

        // bin.process_spans([&](std::vector<uint32_t>::const_iterator mem_ptr, uint64_t addr, uint32_t len_words) {
        //     // Move data from private memory into L1 to be copied into private memory during kernel init
        //     uint32_t relo_addr = hal.relocate_dev_addr(addr, local_init);
        //     if (relo_addr != addr) {
        //         // Local memory relocated to L1 for copying in kernel init
        //         chip->write_to_device(&*mem_ptr, len_words * sizeof(uint32_t), tunnel_1x.mmio_cxy_virtual(),
        //         relo_addr); log_info(
        //             LogSiliconDriver,
        //             "Writing local memory to {:#x} -> reloc {:#x} size {} B",
        //             addr,
        //             relo_addr,
        //             len_words * sizeof(uint32_t));
        //     } else {
        //         config.binary_addr = relo_addr;
        //         config.binary_size = len_words * sizeof(uint32_t);
        //         config.binary_size = (config.binary_size + 15) & ~0xF;

        //         chip->write_to_device(
        //             (void*)&config, sizeof(lite_fabric::LiteFabricConfig), tunnel_1x.mmio_cxy_virtual(),
        //             config_addr);

        //         log_info(
        //             LogSiliconDriver,
        //             "Writing binary to {:#x} -> reloc {:#x} size {} B",
        //             addr,
        //             relo_addr,
        //             len_words * sizeof(uint32_t));
        //         chip->write_to_device(&*mem_ptr, len_words * sizeof(uint32_t), tunnel_1x.mmio_cxy_virtual(),
        //         relo_addr);
        //     }
        // });

        // log_info(
        //     LogSiliconDriver,
        //     "Wrote lite fabric. Core: {}, Config: {:#x}, Binary: {:#x}, Size: {} B",
        //     tunnel_1x.mmio_core_logical,
        //     config_addr,
        //     config.binary_addr,
        //     config.binary_size);
    }

    chip->l1_membar();

    for (auto tunnel_1x : desc.tunnels_from_mmio) {
        set_reset_state(chip, tunnel_1x.mmio_cxy_virtual(), false);
    }

    chip->l1_membar();
    // Wait for ready
    for (auto tunnel_1x : desc.tunnels_from_mmio) {
        wait_for_state(chip, tunnel_1x.mmio_cxy_virtual(), get_state_address(), InitState::READY);
        // log_info(
        //     LogSiliconDriver,
        //     "Lite Fabric {} {} (virtual={}) is ready",
        //     tunnel_1x.mmio_core_logical,
        //     tunnel_1x.mmio_core_virtual.y,
        //     tunnel_1x.mmio_core_virtual.x);
    }
}

void terminate_lite_fabric(Chip* chip, const SystemDescriptor& desc) {
    uint32_t routing_enabled_address = MEM_LITE_FABRIC_CONFIG_BASE + offsetof(LiteFabricMemoryMap, config) +
                                       offsetof(LiteFabricConfig, routing_enabled);
    uint32_t enabled = 0;
    for (const auto& tunnel_1x : desc.tunnels_from_mmio) {
        // log_info(
        //     LogSiliconDriver,
        //     "Host to terminate Device {} {} (virtual={})",
        //     0,
        //     tunnel_1x.mmio_core_logical,
        //     tunnel_1x.mmio_core_virtual);
        CoreCoord umd_core = CoreCoord(
            tunnel_1x.mmio_core_virtual.x, tunnel_1x.mmio_core_virtual.y, CoreType::ETH, CoordSystem::TRANSLATED);
        chip->write_to_device(umd_core, (void*)&enabled, routing_enabled_address, sizeof(uint32_t));
    }
    chip->l1_membar();
}

}  // namespace lite_fabric

}  // namespace tt::umd
