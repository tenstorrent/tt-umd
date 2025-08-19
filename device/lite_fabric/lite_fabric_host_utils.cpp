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

uint64_t relocate_dev_addr(uint64_t addr, uint64_t local_init_addr) {
    if ((addr & MEM_LOCAL_BASE) == MEM_LOCAL_BASE) {
        return (addr & ~MEM_LOCAL_BASE) + local_init_addr;
    }

    return addr;
}

}  // namespace

namespace tt::umd {

namespace lite_fabric {

std::vector<uint8_t> get_kernel_from_hex(const std::filesystem::path& hex_path) {
    std::ifstream file(hex_path);
    std::vector<uint8_t> data;

    if (!file) {
        throw std::runtime_error("cannot open file: " + hex_path.string());
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] != ':') {
            continue;
        }

        int byte_count = std::stoi(line.substr(1, 2), nullptr, 16);
        int address = std::stoi(line.substr(3, 4), nullptr, 16);
        int record_type = std::stoi(line.substr(7, 2), nullptr, 16);

        if (record_type == 0) {  // data record
            for (int i = 0; i < byte_count; ++i) {
                int byte_val = std::stoi(line.substr(9 + i * 2, 2), nullptr, 16);
                data.push_back(static_cast<uint8_t>(byte_val));
            }
        } else if (record_type == 1) {  // EOF record
            break;
        }
    }

    return data;
}

// TODO(pjanevski): Is this the same as the function below?
uint32_t get_eth_channel_mask(Chip* chip, const std::vector<CoreCoord>& eth_cores) {
    uint32_t mask = 0;
    for (const auto& eth_core : eth_cores) {
        CoreCoord logical_core = chip->get_soc_descriptor().translate_coord_to(eth_core, CoordSystem::LOGICAL);
        mask |= 0x1 << logical_core.y;
    }
    return mask;
}

// uint32_t get_eth_channel_mask(chip_id_t device_id) {
//     // auto& cp = tt::tt_metal::MetalContext::instance().get_control_plane();

//     // uint32_t mask = 0;
//     // for (const auto& core : cp.get_active_ethernet_cores(device_id)) {
//     //     mask |= 0x1 << core.y;
//     // }

//     uint32_t mask = 0;
//     return mask;
// }

SystemDescriptor get_system_descriptor_2_devices(chip_id_t mmio_device_id, chip_id_t connected_device_id) {
    SystemDescriptor desc;
    return desc;
}

uint32_t get_local_init_addr() {
    // TODO(pjanevski): Implement this
    return 0;
}

void set_reset_state(Chip* chip, CoreCoord eth_core, bool assert_reset) {
    // We run on DM1. Don't touch DM0. It is running base firmware
    TensixSoftResetOptions reset_val = TENSIX_ASSERT_SOFT_RESET;
    if (assert_reset) {
        reset_val = reset_val & static_cast<TensixSoftResetOptions>(
                                    ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::BRISC));

        chip->set_tensix_risc_reset(eth_core, reset_val);
    } else {
        reset_val = TENSIX_DEASSERT_SOFT_RESET &
                    static_cast<TensixSoftResetOptions>(
                        ~std::underlying_type<TensixSoftResetOptions>::type(TensixSoftResetOptions::TRISC0));

        chip->unset_tensix_risc_reset(eth_core, reset_val);
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
    constexpr uint32_t k_FirmwareStart = MEM_LITE_FABRIC_FIRMWARE_BASE;
    constexpr uint32_t k_PcResetAddress = MEM_LITE_FABRIC_RESET_PC;

    LiteFabricConfig config{};
    config.is_primary = true;
    config.is_mmio = true;
    config.initial_state = InitState::ETH_INIT_NEIGHBOUR;
    config.current_state = InitState::ETH_INIT_NEIGHBOUR;
    config.binary_addr = 0;
    config.binary_size = 0;

    config.eth_chans_mask = get_eth_channel_mask(chip, eth_cores);
    // config.eth_chans_mask = desc.enabled_eth_channels.at(0);

    config.routing_enabled = true;

    // Need an abstraction layer for Lite Fabric
    auto config_addr = MEM_LITE_FABRIC_CONFIG_BASE;

    for (const auto& tunnel_1x : eth_cores) {
        set_reset_state(chip, tunnel_1x, true);
        set_pc(chip, tunnel_1x, k_PcResetAddress, k_FirmwareStart);

        auto lite_fabric_hex = get_kernel_from_hex("lite_fabric.hex");

        // const ll_api::memory& bin = ll_api::memory(elf_path.string(), ll_api::memory::Loading::DISCRETE);

        auto local_init = MEM_LITE_FABRIC_INIT_LOCAL_L1_BASE_SCRATCH;

        auto lambda_fabric = [&](std::vector<uint32_t>::const_iterator mem_ptr, uint64_t addr, uint32_t len_words) {
            // Move data from private memory into L1 to be copied into private memory during kernel init
            uint32_t relo_addr = relocate_dev_addr(addr, local_init);
            if (relo_addr != addr) {
                // Local memory relocated to L1 for copying in kernel init
                chip->write_to_device(tunnel_1x, &*mem_ptr, relo_addr, len_words * sizeof(uint32_t));
                log_info(
                    LogSiliconDriver,
                    "Writing local memory to {:#x} -> reloc {:#x} size {} B",
                    addr,
                    relo_addr,
                    len_words * sizeof(uint32_t));
            } else {
                config.binary_addr = relo_addr;
                config.binary_size = len_words * sizeof(uint32_t);
                config.binary_size = (config.binary_size + 15) & ~0xF;

                chip->write_to_device(tunnel_1x, (void*)&config, config_addr, sizeof(lite_fabric::LiteFabricConfig));

                log_info(
                    LogSiliconDriver,
                    "Writing binary to {:#x} -> reloc {:#x} size {} B",
                    addr,
                    relo_addr,
                    len_words * sizeof(uint32_t));

                chip->write_to_device(tunnel_1x, &*mem_ptr, relo_addr, len_words * sizeof(uint32_t));
            }
        };

        // TODO(pjanevski): figure out this call
        // lambda_fabric(lite_fabric_hex.begin(), local_init, lite_fabric_hex.size());

        // log_info(
        //     LogSiliconDriver,
        //     "Wrote lite fabric. Core: {}, {}, Config: {:#x}, Binary: {:#x}, Size: {} B",
        //     tunnel_1x.x,
        //     tunnel_1x.y,
        //     config_addr,
        //     config.binary_addr,
        //     config.binary_size);
    }

    chip->l1_membar();

    for (auto tunnel_1x : eth_cores) {
        set_reset_state(chip, tunnel_1x, false);
    }

    chip->l1_membar();

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
