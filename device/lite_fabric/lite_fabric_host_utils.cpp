// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/lite_fabric/lite_fabric_host_utils.hpp"

#include <cstdint>
#include <fstream>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip/chip.hpp"
#include "umd/device/lite_fabric/lf_dev_mem_map.hpp"
#include "umd/device/lite_fabric/lite_fabric.hpp"
#include "umd/device/types/xy_pair.hpp"

static const uint8_t lite_fabric_bin[] = {
#include "lite_fabric.embed"
};

namespace {
uint32_t get_state_address() {
    return LITE_FABRIC_CONFIG_START + offsetof(tt::umd::lite_fabric::LiteFabricConfig, current_state);
}

}  // namespace

namespace tt::umd {

namespace lite_fabric {

uint32_t get_eth_channel_mask(const SocDescriptor& soc_desc, const std::vector<CoreCoord>& eth_cores) {
    uint32_t mask = 0;
    for (const auto& eth_core : eth_cores) {
        mask |= 0x1 << soc_desc.get_eth_channel_for_core(eth_core);
    }
    return mask;
}

void set_reset_state(TTDevice* tt_device, tt_xy_pair translated_core, bool assert_reset) {
    // Lite fabric on blackhole runs on DM1. Don't touch DM0. It is running base firmware.
    TensixSoftResetOptions reset_val = TENSIX_ASSERT_SOFT_RESET;
    if (assert_reset) {
        reset_val = reset_val & static_cast<TensixSoftResetOptions>(
                                    ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::BRISC));

        tt_device->set_risc_reset_state(translated_core, static_cast<uint32_t>(reset_val & ALL_TENSIX_SOFT_RESET));
    } else {
        reset_val = TENSIX_DEASSERT_SOFT_RESET &
                    static_cast<TensixSoftResetOptions>(
                        ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::TRISC0));

        tt_device->set_risc_reset_state(translated_core, static_cast<uint32_t>(reset_val & ALL_TENSIX_SOFT_RESET));
    }
}

void set_pc(TTDevice* tt_device, tt_xy_pair eth_core, uint32_t pc_addr, uint32_t pc_val) {
    tt_device->write_to_device((void*)&pc_val, eth_core, pc_addr, sizeof(uint32_t));
}

void wait_for_state(TTDevice* tt_device, tt_xy_pair translated_core, uint32_t addr, InitState state) {
    std::vector<uint32_t> readback{static_cast<uint32_t>(lite_fabric::InitState::UNKNOWN)};
    while (static_cast<InitState>(readback[0]) != state) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        tt_device->read_from_device(readback.data(), translated_core, addr, sizeof(uint32_t));
    }
}

void launch_lite_fabric(TTDevice* tt_device, const SocDescriptor& soc_desc, const std::vector<CoreCoord>& eth_cores) {
    constexpr uint32_t k_FirmwareStart = 0x6a000;
    constexpr uint32_t k_PcResetAddress = LITE_FABRIC_RESET_PC;

    LiteFabricConfig config{};
    config.is_primary = true;
    config.is_mmio = true;
    config.initial_state = InitState::ETH_INIT_NEIGHBOUR;
    config.current_state = InitState::ETH_INIT_NEIGHBOUR;
    config.binary_addr = 0;
    config.binary_size = 0;
    config.eth_chans_mask = get_eth_channel_mask(soc_desc, eth_cores);
    config.routing_enabled = true;

    size_t bin_size = sizeof(lite_fabric_bin) / sizeof(lite_fabric_bin[0]);

    // Set up configuration
    config.binary_addr = k_FirmwareStart;
    config.binary_size = (bin_size + 15) & ~0xF;

    // Need an abstraction layer for Lite Fabric
    auto config_addr = LITE_FABRIC_CONFIG_START;

    for (const auto& tunnel_1x : eth_cores) {
        set_reset_state(tt_device, tunnel_1x, true);
        set_pc(tt_device, tunnel_1x, k_PcResetAddress, k_FirmwareStart);

        tt_device->write_to_device(
            (void*)&config,
            soc_desc.translate_chip_coord_to_translated(tunnel_1x),
            config_addr,
            sizeof(LiteFabricConfig));
        tt_device->write_to_device(
            lite_fabric_bin, soc_desc.translate_chip_coord_to_translated(tunnel_1x), k_FirmwareStart, bin_size);
    }

    std::unordered_set<CoreCoord> eth_cores_set(eth_cores.begin(), eth_cores.end());

    for (auto tunnel_1x : eth_cores) {
        set_reset_state(tt_device, tunnel_1x, false);
    }

    for (auto tunnel_1x : eth_cores) {
        wait_for_state(tt_device, tunnel_1x, get_state_address(), InitState::READY);
        log_debug(LogUMD, "Lite Fabric ready on core ({}, {})", tunnel_1x.x, tunnel_1x.y);
    }
}

void terminate_lite_fabric(
    TTDevice* tt_device, const SocDescriptor& soc_desc, const std::vector<CoreCoord>& eth_cores) {
    uint32_t routing_enabled_address =
        LITE_FABRIC_CONFIG_START + offsetof(LiteFabricMemoryMap, config) + offsetof(LiteFabricConfig, routing_enabled);
    uint32_t enabled = 0;
    for (const auto& tunnel_1x : eth_cores) {
        {
            log_debug(LogUMD, "Host to terminate lite fabric on core ({}, {})", tunnel_1x.x, tunnel_1x.y);
            tt_device->write_to_device(
                (void*)&enabled,
                soc_desc.translate_chip_coord_to_translated(tunnel_1x),
                routing_enabled_address,
                sizeof(uint32_t));
        }
    }
}

}  // namespace lite_fabric

}  // namespace tt::umd
