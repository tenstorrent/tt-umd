// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/lite_fabric/lite_fabric_host_utils.hpp"

#include <fstream>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip/chip.hpp"
#include "umd/device/lite_fabric/lf_dev_mem_map.hpp"
#include "umd/device/lite_fabric/lite_fabric.hpp"

namespace {
uint32_t get_state_address() {
    return LITE_FABRIC_CONFIG_START + offsetof(tt::umd::lite_fabric::LiteFabricConfig, current_state);
}

}  // namespace

namespace tt::umd {

namespace lite_fabric {

std::vector<uint8_t> read_binary_file(const std::string& file_name) {
    std::ifstream bin_file(file_name, std::ios::binary);
    if (!bin_file) {
        throw std::runtime_error(fmt::format("Failed to open binary file: {}", file_name));
    }

    bin_file.seekg(0, std::ios::end);
    size_t bin_size = bin_file.tellg();
    bin_file.seekg(0, std::ios::beg);

    std::vector<uint8_t> binary_data(bin_size);
    bin_file.read(reinterpret_cast<char*>(binary_data.data()), bin_size);
    bin_file.close();

    return binary_data;
}

uint32_t get_eth_channel_mask(Chip* chip, const std::vector<CoreCoord>& eth_cores) {
    uint32_t mask = 0;
    for (const auto& eth_core : eth_cores) {
        mask |= 0x1 << chip->get_soc_descriptor().get_eth_channel_for_core(eth_core);
    }
    return mask;
}

void set_reset_state(Chip* chip, CoreCoord eth_core, bool assert_reset) {
    // Lite fabric on blackhole runs on DM1. Don't touch DM0. It is running base firmware.
    TensixSoftResetOptions reset_val = TENSIX_ASSERT_SOFT_RESET;
    if (assert_reset) {
        reset_val = reset_val & static_cast<TensixSoftResetOptions>(
                                    ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::BRISC));

        chip->send_tensix_risc_reset(eth_core, reset_val);
    } else {
        reset_val = TENSIX_DEASSERT_SOFT_RESET &
                    static_cast<TensixSoftResetOptions>(
                        ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::TRISC0));

        chip->send_tensix_risc_reset(eth_core, reset_val);
    }
}

void set_pc(Chip* chip, CoreCoord eth_core, uint32_t pc_addr, uint32_t pc_val) {
    chip->write_to_device(eth_core, (void*)&pc_val, pc_addr, sizeof(uint32_t));
}

void wait_for_state(Chip* chip, CoreCoord eth_core, uint32_t addr, InitState state) {
    std::vector<uint32_t> readback{static_cast<uint32_t>(lite_fabric::InitState::UNKNOWN)};
    while (static_cast<InitState>(readback[0]) != state) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        chip->read_from_device(eth_core, readback.data(), addr, sizeof(uint32_t));
    }
}

void launch_lite_fabric(Chip* chip, const std::vector<CoreCoord>& eth_cores) {
    constexpr uint32_t k_FirmwareStart = 0x6a000;
    constexpr uint32_t k_PcResetAddress = LITE_FABRIC_RESET_PC;

    LiteFabricConfig config{};
    config.is_primary = true;
    config.is_mmio = true;
    config.initial_state = InitState::ETH_INIT_NEIGHBOUR;
    config.current_state = InitState::ETH_INIT_NEIGHBOUR;
    config.binary_addr = 0;
    config.binary_size = 0;
    config.eth_chans_mask = get_eth_channel_mask(chip, eth_cores);
    config.routing_enabled = true;

    // Need an abstraction layer for Lite Fabric
    auto config_addr = LITE_FABRIC_CONFIG_START;

    for (const auto& tunnel_1x : eth_cores) {
        set_reset_state(chip, tunnel_1x, true);
        set_pc(chip, tunnel_1x, k_PcResetAddress, k_FirmwareStart);

        std::vector<uint8_t> binary_data = read_binary_file("device/libs/lite_fabric.bin");
        size_t bin_size = binary_data.size();

        // Set up configuration
        config.binary_addr = k_FirmwareStart;
        config.binary_size = (bin_size + 15) & ~0xF;

        chip->write_to_device(tunnel_1x, (void*)&config, config_addr, sizeof(lite_fabric::LiteFabricConfig));

        chip->write_to_device(tunnel_1x, binary_data.data(), k_FirmwareStart, bin_size);
    }

    std::unordered_set<CoreCoord> eth_cores_set(eth_cores.begin(), eth_cores.end());

    chip->l1_membar(eth_cores_set);

    for (auto tunnel_1x : eth_cores) {
        set_reset_state(chip, tunnel_1x, false);
    }

    for (auto tunnel_1x : eth_cores) {
        wait_for_state(chip, tunnel_1x, get_state_address(), InitState::READY);
        log_info(LogSiliconDriver, "Lite Fabric ready on core ({}, {})", tunnel_1x.x, tunnel_1x.y);
    }
}

void terminate_lite_fabric(Chip* chip, const std::vector<CoreCoord>& eth_cores) {
    uint32_t routing_enabled_address =
        LITE_FABRIC_CONFIG_START + offsetof(LiteFabricMemoryMap, config) + offsetof(LiteFabricConfig, routing_enabled);
    uint32_t enabled = 0;
    for (const auto& tunnel_1x : eth_cores) {
        log_info(LogSiliconDriver, "Host to terminate lite fabric on core ({}, {})", tunnel_1x.x, tunnel_1x.y);
        chip->write_to_device(tunnel_1x, (void*)&enabled, routing_enabled_address, sizeof(uint32_t));
    }
    chip->l1_membar();
}

}  // namespace lite_fabric

}  // namespace tt::umd
