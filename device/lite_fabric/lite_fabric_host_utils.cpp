// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/lite_fabric/lite_fabric_host_utils.h"

#include <fstream>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip/chip.h"
#include "umd/device/lite_fabric/blackhole_dev_mem_map.h"
#include "umd/device/lite_fabric/lite_fabric.h"

namespace {
uint32_t get_state_address() {
    return MEM_LITE_FABRIC_CONFIG_BASE + offsetof(tt::umd::lite_fabric::LiteFabricConfig, current_state);
}

}  // namespace

namespace tt::umd {

namespace lite_fabric {

std::vector<uint8_t> read_binary_file(const std::string& file_name) {
    std::ifstream file(file_name, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + file_name);
    }

    // Get file size
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Allocate vector and read contents
    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        throw std::runtime_error("Failed to read file: " + file_name);
    }

    return buffer;
}

std::vector<uint32_t> read_binary_file_u32(const std::string& file_name) {
    std::ifstream file(file_name, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + file_name);
    }

    // Get file size
    std::streamsize file_size = file.tellg();
    if (file_size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("File size is not a multiple of 4 bytes");
    }

    file.seekg(0, std::ios::beg);

    // Allocate vector and read contents
    size_t num_elements = file_size / sizeof(uint32_t);
    std::vector<uint32_t> buffer(num_elements);

    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        throw std::runtime_error("Failed to read file: " + file_name);
    }

    return buffer;
}

uint32_t get_eth_channel_mask(Chip* chip, const std::vector<CoreCoord>& eth_cores) {
    uint32_t mask = 0;
    for (const auto& eth_core : eth_cores) {
        CoreCoord logical_core = chip->get_soc_descriptor().translate_coord_to(eth_core, CoordSystem::LOGICAL);
        mask |= 0x1 << logical_core.y;
    }
    return mask;
}

void set_reset_state(Chip* chip, CoreCoord eth_core, bool assert_reset) {
    // We run on DM1. Don't touch DM0. It is running base firmware
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
    constexpr uint32_t k_PcResetAddress = MEM_LITE_FABRIC_RESET_PC;

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
    auto config_addr = MEM_LITE_FABRIC_CONFIG_BASE;

    for (const auto& tunnel_1x : eth_cores) {
        set_reset_state(chip, tunnel_1x, true);
        set_pc(chip, tunnel_1x, k_PcResetAddress, k_FirmwareStart);

        std::ifstream bin_file("lite_fabric.bin", std::ios::binary);
        if (!bin_file) {
            // throw std::runtime_error(fmt::format("Failed to open binary file: {}", bin_path));
        }

        // Get file size
        bin_file.seekg(0, std::ios::end);
        size_t bin_size = bin_file.tellg();
        bin_file.seekg(0, std::ios::beg);

        // Read entire binary into memory
        std::vector<uint8_t> binary_data(bin_size);
        bin_file.read(reinterpret_cast<char*>(binary_data.data()), bin_size);
        bin_file.close();

        // Set up configuration
        config.binary_addr = k_FirmwareStart;
        config.binary_size = (bin_size + 15) & ~0xF;

        std::cout << "size of lite fabric config " << sizeof(lite_fabric::LiteFabricConfig) << std::endl;

        chip->write_to_device(tunnel_1x, (void*)&config, config_addr, sizeof(lite_fabric::LiteFabricConfig));

        // TODO: check if logic for waiting for state even works, not needed in main code path.
        // wait_for_state(chip, tunnel_1x, get_state_address(), InitState::ETH_INIT_NEIGHBOUR);

        std::cout << "bin size " << bin_size << std::endl;
        chip->write_to_device(tunnel_1x, binary_data.data(), k_FirmwareStart, bin_size);

        // log_info(
        //     LogSiliconDriver,
        //     "Wrote lite fabric. Core: {}, {}, Config: {:#x}, Binary: {:#x}, Size: {} B",
        //     tunnel_1x.x,
        //     tunnel_1x.y,
        //     config_addr,
        //     config.binary_addr,
        //     config.binary_size);
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));

    // create unordered set from vector of eth cores
    std::unordered_set<CoreCoord> eth_cores_set(eth_cores.begin(), eth_cores.end());

    chip->l1_membar(eth_cores_set);

    for (auto tunnel_1x : eth_cores) {
        set_reset_state(chip, tunnel_1x, false);
    }

    chip->l1_membar(eth_cores_set);

    auto state_addr = get_state_address();
    std::cout << "state_addr 0x" << std::hex << state_addr << std::dec << std::endl;

    // Wait for ready
    for (auto tunnel_1x : eth_cores) {
        wait_for_state(chip, tunnel_1x, get_state_address(), InitState::READY);
        log_info(LogSiliconDriver, "Lite Fabric ready on core (translated={}, {})", tunnel_1x.x, tunnel_1x.y);
    }
}

void terminate_lite_fabric(Chip* chip, const std::vector<CoreCoord>& eth_cores) {
    uint32_t routing_enabled_address = MEM_LITE_FABRIC_CONFIG_BASE + offsetof(LiteFabricMemoryMap, config) +
                                       offsetof(LiteFabricConfig, routing_enabled);
    uint32_t enabled = 0;
    for (const auto& tunnel_1x : eth_cores) {
        log_info(
            LogSiliconDriver, "Host to terminate lite fabric on core (translated={}, {})", 0, tunnel_1x.x, tunnel_1x.y);
        chip->write_to_device(tunnel_1x, (void*)&enabled, routing_enabled_address, sizeof(uint32_t));
    }
    chip->l1_membar();
}

}  // namespace lite_fabric

}  // namespace tt::umd
