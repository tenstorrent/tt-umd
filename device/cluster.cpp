// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/cluster.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ratio>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "api/umd/device/cluster.h"
#include "api/umd/device/tt_core_coordinates.h"
#include "logger.hpp"
#include "umd/device/architecture_implementation.h"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/mock_chip.h"
#include "umd/device/chip/remote_chip.h"
#include "umd/device/chip_helpers/tlb_manager.h"
#include "umd/device/driver_atomics.h"
#include "umd/device/hugepage.h"
#include "umd/device/topology_utils.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/tt_core_coordinates.h"
#include "umd/device/tt_soc_descriptor.h"
#include "umd/device/types/arch.h"
#include "umd/device/types/blackhole_arc.h"
#include "umd/device/types/blackhole_eth.h"
#include "umd/device/types/tlb.h"
#include "umd/device/umd_utils.h"
#include "umd/device/wormhole_implementation.h"
#include "yaml-cpp/yaml.h"

using namespace tt;
using namespace tt::umd;

extern bool umd_use_noc1;

static const uint32_t MSG_ERROR_REPLY = 0xFFFFFFFF;

// Remove 256MB from full 1GB for channel 3 (iATU limitation)
static constexpr uint32_t HUGEPAGE_CHANNEL_3_SIZE_LIMIT = 805306368;

static constexpr uint32_t REMOTE_CMD_NOC_BIT = 9;

// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------

#include <fstream>
#include <iomanip>
#include <thread>

#include "umd/device/tt_silicon_driver_common.hpp"
#include "umd/device/tt_xy_pair.h"

struct routing_cmd_t {
    uint64_t sys_addr;
    uint32_t data;
    uint32_t flags;
    uint16_t rack;
    uint16_t src_resp_buf_index;
    uint32_t local_buf_index;
    uint8_t src_resp_q_id;
    uint8_t host_mem_txn_id;
    uint16_t padding;
    uint32_t src_addr_tag;  // upper 32-bits of request source address.
};

struct remote_update_ptr_t {
    uint32_t ptr;
    uint32_t pad[3];
};

namespace tt::umd {

const tt_SocDescriptor& Cluster::get_soc_descriptor(chip_id_t chip_id) const {
    return get_chip(chip_id)->get_soc_descriptor();
}

void Cluster::create_device(
    const std::set<chip_id_t>& target_mmio_device_ids,
    const uint32_t& num_host_mem_ch_per_mmio_device,
    const bool create_mock_chips) {
    log_debug(LogSiliconDriver, "Cluster::Cluster");

    // Don't buffer stdout.
    setbuf(stdout, NULL);

    for (const chip_id_t& logical_device_id : target_mmio_device_ids) {
        if (!create_mock_chips) {
            bool hugepages_initialized =
                (get_local_chip(logical_device_id)->get_sysmem_manager()->get_hugepage_mapping(0).mapping != nullptr);
            // Large writes to remote chips require hugepages to be initialized.
            // Conservative assert - end workload if remote chips present but hugepages not initialized (failures caused
            // if using remote only for small transactions)
            if (remote_chip_ids_.size()) {
                log_assert(
                    hugepages_initialized,
                    "Hugepages must be successfully initialized if workload contains remote chips!");
            }
            if (!hugepages_initialized) {
                log_warning(LogSiliconDriver, "No hugepage mapping at device {}.", logical_device_id);
            }
        }
    }

    for (const chip_id_t& chip : all_chip_ids_) {
        if (!cluster_desc->is_chip_mmio_capable(chip)) {
            flush_non_mmio_per_chip[chip] = false;
        }
    }
}

void Cluster::construct_cluster(const uint32_t& num_host_mem_ch_per_mmio_device, const bool create_mock_chips) {
    // TODO: work on removing this member altogether. Currently assumes all have the same arch.
    arch_name = chips_.empty() ? tt::ARCH::Invalid : chips_.begin()->second->get_soc_descriptor().arch;

    if (!create_mock_chips) {
        auto available_device_ids = detect_available_device_ids();
        log_info(LogSiliconDriver, "Detected PCI devices: {}", available_device_ids);
        log_info(
            LogSiliconDriver, "Using local chip ids: {} and remote chip ids {}", local_chip_ids_, remote_chip_ids_);
    }

    create_device(local_chip_ids_, num_host_mem_ch_per_mmio_device, create_mock_chips);

    // Disable dependency to ethernet firmware for all BH devices and WH devices with all chips having MMIO (e.g. UBB
    // Galaxy, or P300).
    if (remote_chip_ids_.empty()) {
        use_ethernet_ordered_writes = false;
        use_ethernet_broadcast = false;
    }

    populate_cores();

    // MT: Initial BH - skip this for BH
    if (arch_name == tt::ARCH::WORMHOLE_B0) {
        remote_transfer_ethernet_cores.resize(local_chip_ids_.size());
        for (const auto& logical_mmio_chip_id : local_chip_ids_) {
            const tt_SocDescriptor& soc_desc = get_soc_descriptor(logical_mmio_chip_id);
            // 4-5 is for send_epoch_commands, 0-3 are for everything else
            for (std::uint32_t i = 0; i < NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS; i++) {
                if (remote_transfer_ethernet_cores.size() <= logical_mmio_chip_id) {
                    remote_transfer_ethernet_cores.resize(logical_mmio_chip_id + 1);
                }
                CoreCoord ethernet_core = soc_desc.get_eth_core_for_channel(i, CoordSystem::VIRTUAL);
                remote_transfer_ethernet_cores.at(logical_mmio_chip_id)
                    .push_back(tt_cxy_pair(logical_mmio_chip_id, ethernet_core));
            }
        }
    }
}

std::unique_ptr<Chip> Cluster::construct_chip_from_cluster(
    chip_id_t chip_id,
    tt_ClusterDescriptor* cluster_desc,
    tt_SocDescriptor& soc_desc,
    int num_host_mem_channels,
    const bool clean_system_resources,
    const bool create_mock_chip) {
    if (create_mock_chip) {
        return std::make_unique<MockChip>(soc_desc);
    }

    if (cluster_desc->is_chip_mmio_capable(chip_id)) {
        return std::make_unique<LocalChip>(
            soc_desc, cluster_desc->get_chips_with_mmio().at(chip_id), num_host_mem_channels, clean_system_resources);
    } else {
        return std::make_unique<RemoteChip>(soc_desc);
    }
}

std::unique_ptr<Chip> Cluster::construct_chip_from_cluster(
    const std::string& soc_desc_path,
    chip_id_t chip_id,
    tt_ClusterDescriptor* cluster_desc,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks,
    int num_host_mem_channels,
    const bool clean_system_resources,
    const bool create_mock_chip) {
    HarvestingMasks harvesting_masks =
        get_harvesting_masks(chip_id, cluster_desc, perform_harvesting, simulated_harvesting_masks);
    const BoardType chip_board_type = cluster_desc->get_board_type(chip_id);
    std::optional<ChipUID> chip_uid = cluster_desc->get_chip_uid(chip_id);
    uint8_t asic_location = chip_uid.has_value() ? chip_uid.value().asic_location : 0;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(
        soc_desc_path,
        cluster_desc->get_noc_translation_table_en().at(chip_id),
        harvesting_masks,
        chip_board_type,
        asic_location);
    return construct_chip_from_cluster(
        chip_id, cluster_desc, soc_desc, num_host_mem_channels, clean_system_resources, create_mock_chip);
}

std::unique_ptr<Chip> Cluster::construct_chip_from_cluster(
    chip_id_t chip_id,
    tt_ClusterDescriptor* cluster_desc,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks,
    int num_host_mem_channels,
    const bool clean_system_resources,
    const bool create_mock_chip) {
    tt::ARCH arch = cluster_desc->get_arch(chip_id);
    HarvestingMasks harvesting_masks =
        get_harvesting_masks(chip_id, cluster_desc, perform_harvesting, simulated_harvesting_masks);
    const BoardType chip_board_type = cluster_desc->get_board_type(chip_id);
    std::optional<ChipUID> chip_uid = cluster_desc->get_chip_uid(chip_id);
    uint8_t asic_location = chip_uid.has_value() ? chip_uid.value().asic_location : 0;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(
        arch,
        cluster_desc->get_noc_translation_table_en().at(chip_id),
        harvesting_masks,
        chip_board_type,
        asic_location);
    return construct_chip_from_cluster(
        chip_id, cluster_desc, soc_desc, num_host_mem_channels, clean_system_resources, create_mock_chip);
}

void Cluster::add_chip(chip_id_t chip_id, std::unique_ptr<Chip> chip) {
    log_assert(
        chips_.find(chip_id) == chips_.end(),
        "Chip with id {} already exists in cluster. Cannot add another chip with the same id.",
        chip_id);
    all_chip_ids_.insert(chip_id);
    if (cluster_desc->is_chip_mmio_capable(chip_id)) {
        local_chip_ids_.insert(chip_id);
    } else {
        remote_chip_ids_.insert(chip_id);
    }
    chips_.emplace(chip_id, std::move(chip));
}

uint32_t Cluster::get_tensix_harvesting_mask(
    chip_id_t chip_id,
    tt_ClusterDescriptor* cluster_desc,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks) {
    if (!perform_harvesting) {
        log_info(LogSiliconDriver, "Skipping harvesting for chip {}.", chip_id);
        return 0;
    }
    uint32_t tensix_harvesting_mask_physical_layout = cluster_desc->get_harvesting_info().at(chip_id);
    uint32_t tensix_harvesting_mask = CoordinateManager::shuffle_tensix_harvesting_mask(
        cluster_desc->get_arch(chip_id), tensix_harvesting_mask_physical_layout);
    uint32_t simulated_harvesting_mask = (simulated_harvesting_masks.find(chip_id) != simulated_harvesting_masks.end())
                                             ? simulated_harvesting_masks.at(chip_id).tensix_harvesting_mask
                                             : 0;
    log_info(
        LogSiliconDriver,
        "Harvesting mask for chip {} is 0x{:x} (physical layout: 0x{:x}, logical: 0x{:x}, simulated harvesting mask: "
        "0x{:x}).",
        chip_id,
        tensix_harvesting_mask | simulated_harvesting_mask,
        tensix_harvesting_mask_physical_layout,
        tensix_harvesting_mask,
        simulated_harvesting_mask);
    return tensix_harvesting_mask | simulated_harvesting_mask;
}

uint32_t Cluster::get_dram_harvesting_mask(
    chip_id_t chip_id,
    tt_ClusterDescriptor* cluster_desc,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks) {
    if (!perform_harvesting) {
        log_info(LogSiliconDriver, "Skipping DRAM harvesting for chip {}.", chip_id);
        return 0;
    }

    return simulated_harvesting_masks.find(chip_id) != simulated_harvesting_masks.end()
               ? cluster_desc->get_dram_harvesting_mask(chip_id) |
                     simulated_harvesting_masks.at(chip_id).dram_harvesting_mask
               : cluster_desc->get_dram_harvesting_mask(chip_id);
}

uint32_t Cluster::get_eth_harvesting_mask(
    chip_id_t chip_id,
    tt_ClusterDescriptor* cluster_desc,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks) {
    if (!perform_harvesting) {
        log_info(LogSiliconDriver, "Skipping ETH harvesting for chip {}.", chip_id);
        return 0;
    }

    return simulated_harvesting_masks.find(chip_id) != simulated_harvesting_masks.end()
               ? cluster_desc->get_eth_harvesting_mask(chip_id) |
                     simulated_harvesting_masks.at(chip_id).eth_harvesting_mask
               : cluster_desc->get_eth_harvesting_mask(chip_id);
}

HarvestingMasks Cluster::get_harvesting_masks(
    chip_id_t chip_id,
    tt_ClusterDescriptor* cluster_desc,
    bool perfrom_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks) {
    return HarvestingMasks{
        .tensix_harvesting_mask =
            get_tensix_harvesting_mask(chip_id, cluster_desc, perfrom_harvesting, simulated_harvesting_masks),
        .dram_harvesting_mask =
            get_dram_harvesting_mask(chip_id, cluster_desc, perfrom_harvesting, simulated_harvesting_masks),
        .eth_harvesting_mask =
            get_eth_harvesting_mask(chip_id, cluster_desc, perfrom_harvesting, simulated_harvesting_masks)};
}

void Cluster::ubb_eth_connections(
    const std::unordered_map<chip_id_t, std::unique_ptr<tt::umd::Chip>>& chips,
    std::unique_ptr<tt_ClusterDescriptor>& cluster_desc) {
    const uint64_t conn_info = 0x1200;
    const uint64_t node_info = 0x1100;
    const uint32_t eth_unknown = 0;
    const uint32_t eth_unconnected = 1;
    const uint32_t shelf_offset = 9;
    const uint32_t rack_offset = 10;
    const uint32_t base_addr = 0x1ec0;

    std::unordered_map<uint64_t, chip_id_t> chip_uid_to_local_chip_id = {};

    for (const auto& [chip_id, chip] : chips) {
        std::vector<CoreCoord> eth_cores = chip->get_soc_descriptor().get_cores(CoreType::ETH);
        TTDevice* tt_device = chip->get_tt_device();

        uint32_t channel = 0;
        for (const CoreCoord& eth_core : eth_cores) {
            uint32_t port_status;

            tt_device->read_from_device(
                &port_status, tt_xy_pair(eth_core.x, eth_core.y), conn_info + (channel * 4), sizeof(uint32_t));

            if (port_status == eth_unknown || port_status == eth_unconnected) {
                channel++;
                continue;
            }

            uint64_t local_chip_id;
            tt_device->read_from_device(
                &local_chip_id, tt_xy_pair(eth_core.x, eth_core.y), base_addr + (64 * 4), sizeof(uint64_t));

            uint64_t remote_chip_id;
            tt_device->read_from_device(
                &remote_chip_id, tt_cxy_pair(chip_id, eth_core.x, eth_core.y), base_addr + (72 * 4), sizeof(uint64_t));

            chip_uid_to_local_chip_id.insert({local_chip_id, chip_id});

            channel++;
        }
    }

    for (const auto& [chip_id, chip] : chips) {
        std::vector<CoreCoord> eth_cores = chip->get_soc_descriptor().get_cores(CoreType::ETH);
        TTDevice* tt_device = chip->get_tt_device();

        uint32_t channel = 0;
        for (const CoreCoord& eth_core : eth_cores) {
            uint32_t port_status;
            tt_device->read_from_device(
                &port_status,
                tt_cxy_pair(chip_id, eth_core.x, eth_core.y),
                conn_info + (channel * 4),
                sizeof(uint32_t));

            if (port_status == eth_unknown || port_status == eth_unconnected) {
                channel++;
                continue;
            }

            uint64_t remote_chip_id;
            tt_device->read_from_device(
                &remote_chip_id, tt_cxy_pair(chip_id, eth_core.x, eth_core.y), base_addr + (72 * 4), sizeof(uint64_t));

            uint64_t local_chip_id;
            tt_device->read_from_device(
                &local_chip_id, tt_cxy_pair(chip_id, eth_core.x, eth_core.y), base_addr + (64 * 4), sizeof(uint64_t));

            uint32_t remote_eth_id;
            tt_device->read_from_device(
                &remote_eth_id, tt_cxy_pair(chip_id, eth_core.x, eth_core.y), base_addr + 76 * 4, sizeof(uint32_t));

            chip_id_t remote_logical_chip_id = chip_uid_to_local_chip_id.at(remote_chip_id);

            cluster_desc->ethernet_connections[chip_id][channel] = {remote_logical_chip_id, remote_eth_id};

            channel++;
        }
    }
}

Cluster::Cluster(
    const uint32_t& num_host_mem_ch_per_mmio_device,
    const bool create_mock_chips,
    const bool clean_system_resources,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks) {
    cluster_desc = Cluster::create_cluster_descriptor();

    for (auto& chip_id : cluster_desc->get_all_chips()) {
        add_chip(
            chip_id,
            construct_chip_from_cluster(
                chip_id,
                cluster_desc.get(),
                perform_harvesting,
                simulated_harvesting_masks,
                num_host_mem_ch_per_mmio_device,
                clean_system_resources,
                create_mock_chips));
    }

    construct_cluster(num_host_mem_ch_per_mmio_device, create_mock_chips);
}

Cluster::Cluster(
    const std::set<chip_id_t>& target_devices,
    const uint32_t& num_host_mem_ch_per_mmio_device,
    const bool create_mock_chips,
    const bool clean_system_resources,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks) {
    cluster_desc = Cluster::create_cluster_descriptor();

    for (auto& chip_id : target_devices) {
        log_assert(
            cluster_desc->get_all_chips().find(chip_id) != cluster_desc->get_all_chips().end(),
            "Target device {} not present in current cluster!",
            chip_id);
        add_chip(
            chip_id,
            construct_chip_from_cluster(
                chip_id,
                cluster_desc.get(),
                perform_harvesting,
                simulated_harvesting_masks,
                num_host_mem_ch_per_mmio_device,
                clean_system_resources,
                create_mock_chips));
    }

    construct_cluster(num_host_mem_ch_per_mmio_device, create_mock_chips);
}

Cluster::Cluster(
    const std::string& sdesc_path,
    const std::set<chip_id_t>& target_devices,
    const uint32_t& num_host_mem_ch_per_mmio_device,
    const bool create_mock_chips,
    const bool clean_system_resources,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks) {
    cluster_desc = Cluster::create_cluster_descriptor(sdesc_path);

    for (auto& chip_id : target_devices) {
        log_assert(
            cluster_desc->get_all_chips().find(chip_id) != cluster_desc->get_all_chips().end(),
            "Target device {} not present in current cluster!",
            chip_id);
        add_chip(
            chip_id,
            construct_chip_from_cluster(
                sdesc_path,
                chip_id,
                cluster_desc.get(),
                perform_harvesting,
                simulated_harvesting_masks,
                num_host_mem_ch_per_mmio_device,
                clean_system_resources,
                create_mock_chips));
        log_assert(
            cluster_desc->get_arch(chip_id) == get_chip(chip_id)->get_soc_descriptor().arch,
            "Passed soc descriptor has {} arch, but for chip id {} has arch {}",
            arch_to_str(get_chip(chip_id)->get_soc_descriptor().arch),
            chip_id,
            arch_to_str(cluster_desc->get_arch(chip_id)));
    }

    construct_cluster(num_host_mem_ch_per_mmio_device, create_mock_chips);
}

Cluster::Cluster(
    std::unique_ptr<tt_ClusterDescriptor> cluster_descriptor,
    const uint32_t& num_host_mem_ch_per_mmio_device,
    const bool create_mock_chips,
    const bool clean_system_resources,
    bool perform_harvesting,
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks) {
    cluster_desc = std::move(cluster_descriptor);

    for (auto& chip_id : cluster_desc->get_all_chips()) {
        add_chip(
            chip_id,
            construct_chip_from_cluster(
                chip_id,
                cluster_desc.get(),
                perform_harvesting,
                simulated_harvesting_masks,
                num_host_mem_ch_per_mmio_device,
                clean_system_resources,
                create_mock_chips));
    }

    construct_cluster(num_host_mem_ch_per_mmio_device, create_mock_chips);
}

void Cluster::configure_active_ethernet_cores_for_mmio_device(
    chip_id_t mmio_chip, const std::unordered_set<CoreCoord>& active_eth_cores_per_chip) {
    // Makes UMD aware of which ethernet cores have active links.
    // Based on this information, UMD determines which ethernet cores can be used for host->cluster non-MMIO transfers.
    // This overrides the default ethernet cores tagged for host to cluster routing in the constructor and must be
    // called for all MMIO devices, if default behaviour is not desired.
    auto& soc_desc = get_soc_descriptor(mmio_chip);
    log_assert(soc_desc.arch == tt::ARCH::WORMHOLE_B0, "{} can only be called for Wormhole arch", __FUNCTION__);
    // Cores 0, 1, 6, 7 are only available if in the active set
    static std::unordered_set<CoreCoord> eth_cores_available_if_active = {
        soc_desc.get_eth_core_for_channel(0, CoordSystem::VIRTUAL),
        soc_desc.get_eth_core_for_channel(1, CoordSystem::VIRTUAL),
        soc_desc.get_eth_core_for_channel(6, CoordSystem::VIRTUAL),
        soc_desc.get_eth_core_for_channel(7, CoordSystem::VIRTUAL)};
    // Eth cores 8 and 9 are always available
    std::vector<tt_cxy_pair> non_mmio_access_cores_for_chip = {
        {(size_t)mmio_chip, soc_desc.get_eth_core_for_channel(8, CoordSystem::VIRTUAL)},
        {(size_t)mmio_chip, soc_desc.get_eth_core_for_channel(9, CoordSystem::VIRTUAL)}};
    for (const auto& active_eth_core : active_eth_cores_per_chip) {
        const auto virtual_active_eth_core =
            get_soc_descriptor(mmio_chip).translate_coord_to(active_eth_core, CoordSystem::VIRTUAL);
        if (eth_cores_available_if_active.find(virtual_active_eth_core) != eth_cores_available_if_active.end()) {
            non_mmio_access_cores_for_chip.push_back(tt_cxy_pair(mmio_chip, virtual_active_eth_core));
        }
    }

    remote_transfer_ethernet_cores[mmio_chip] = non_mmio_access_cores_for_chip;
    active_eth_core_idx_per_chip.insert({mmio_chip, 0});
    non_mmio_transfer_cores_customized = true;
}

void Cluster::populate_cores() {
    std::uint32_t count = 0;
    for (const auto& [chip_id, chip] : chips_) {
        auto& soc_desc = chip->get_soc_descriptor();
        auto workers = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::VIRTUAL);
        workers_per_chip.insert({chip_id, std::unordered_set<tt_xy_pair>(workers.begin(), workers.end())});
        if (count == 0) {
            auto ethernet_cores = soc_desc.get_cores(CoreType::ETH, CoordSystem::VIRTUAL);
            eth_cores = std::unordered_set<tt_xy_pair>(ethernet_cores.begin(), ethernet_cores.end());
            for (std::uint32_t dram_idx = 0; dram_idx < soc_desc.get_num_dram_channels(); dram_idx++) {
                dram_cores.insert(soc_desc.get_dram_core_for_channel(dram_idx, 0, CoordSystem::VIRTUAL));
            }
        }
        count++;
    }
}

void Cluster::check_pcie_device_initialized(int device_id) {
    TTDevice* tt_device = get_tt_device(device_id);
    tt::ARCH device_arch = tt_device->get_pci_device()->get_arch();
    if (arch_name == tt::ARCH::WORMHOLE_B0) {
        if (device_arch != tt::ARCH::WORMHOLE_B0) {
            throw std::runtime_error(
                fmt::format("Attempted to run wormhole configured tt_device on {}", arch_to_str(device_arch)));
        }
    } else if (arch_name == tt::ARCH::BLACKHOLE) {
        if (device_arch != tt::ARCH::BLACKHOLE) {
            throw std::runtime_error(
                fmt::format("Attempted to run blackhole configured tt_device on {}", arch_to_str(device_arch)));
        }
    } else {
        throw std::runtime_error(fmt::format("Unsupported architecture: {}", arch_to_str(arch_name)));
    }
    auto architecture_implementation = tt_device->get_architecture_implementation();

    // MT Initial BH - Add check for blackhole once access to ARC registers is setup through TLBs
    if (arch_name != tt::ARCH::BLACKHOLE) {
        log_debug(LogSiliconDriver, "== Check if device_id: {} is initialized", device_id);
        uint32_t bar_read_initial =
            bar_read32(device_id, architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4);
        uint32_t arg = bar_read_initial == 500 ? 325 : 500;
        uint32_t bar_read_again;
        uint32_t arc_msg_return = arc_msg(
            device_id,
            0xaa00 | architecture_implementation->get_arc_message_test(),
            true,
            arg,
            0,
            1000,
            &bar_read_again);
        if (arc_msg_return != 0 || bar_read_again != arg + 1) {
            auto postcode = bar_read32(device_id, architecture_implementation->get_arc_reset_scratch_offset());
            throw std::runtime_error(fmt::format(
                "Device is not initialized: arc_fw postcode: {} arc_msg_return: {} arg: {} bar_read_initial: {} "
                "bar_read_again: {}",
                postcode,
                arc_msg_return,
                arg,
                bar_read_initial,
                bar_read_again));
        }
    }

    if (test_setup_interface()) {
        throw std::runtime_error(
            "Device is incorrectly initialized. If this is a harvested Wormhole machine, it is likely that NOC "
            "Translation Tables are not enabled on device. These need to be enabled for the silicon driver to run.");
    }
}

void Cluster::initialize_pcie_devices() {
    log_debug(LogSiliconDriver, "Cluster::start");

    for (auto chip_id : local_chip_ids_) {
        check_pcie_device_initialized(chip_id);
    }

    init_pcie_iatus();

    init_membars();
}

std::set<chip_id_t> Cluster::get_target_device_ids() { return all_chip_ids_; }

std::set<chip_id_t> Cluster::get_target_mmio_device_ids() { return local_chip_ids_; }

std::set<chip_id_t> Cluster::get_target_remote_device_ids() { return remote_chip_ids_; }

void Cluster::assert_risc_reset() { broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET); }

void Cluster::deassert_risc_reset() { broadcast_tensix_risc_reset_to_cluster(TENSIX_DEASSERT_SOFT_RESET); }

void Cluster::deassert_risc_reset_at_core(tt_cxy_pair core, const TensixSoftResetOptions& soft_resets) {
    // Get Target Device to query soc descriptor and determine location in cluster
    std::uint32_t target_device = core.chip;
    CoreCoord core_coord = get_soc_descriptor(target_device).get_coord_at(core, CoordSystem::VIRTUAL);
    log_assert(
        core_coord.core_type == CoreType::TENSIX || core_coord.core_type == CoreType::ETH,
        "Cannot deassert reset on a non-tensix or harvested core");
    bool target_is_mmio_capable = cluster_desc->is_chip_mmio_capable(target_device);
    if (target_is_mmio_capable) {
        send_tensix_risc_reset_to_core(core, soft_resets);
    } else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Can't issue access to remote core in BH");
        send_remote_tensix_risc_reset_to_core(core, soft_resets);
    }
}

void Cluster::deassert_risc_reset_at_core(
    const chip_id_t chip, const CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    deassert_risc_reset_at_core({(size_t)chip, translate_to_api_coords(chip, core)}, soft_resets);
}

void Cluster::assert_risc_reset_at_core(tt_cxy_pair core, const TensixSoftResetOptions& soft_resets) {
    // Get Target Device to query soc descriptor and determine location in cluster
    std::uint32_t target_device = core.chip;
    CoreCoord core_coord = get_soc_descriptor(target_device).get_coord_at(core, CoordSystem::VIRTUAL);
    log_assert(
        core_coord.core_type == CoreType::TENSIX || core_coord.core_type == CoreType::ETH,
        "Cannot assert reset on a non-tensix or harvested core");
    bool target_is_mmio_capable = cluster_desc->is_chip_mmio_capable(target_device);
    if (target_is_mmio_capable) {
        send_tensix_risc_reset_to_core(core, soft_resets);
    } else {
        send_remote_tensix_risc_reset_to_core(core, soft_resets);
    }
}

void Cluster::assert_risc_reset_at_core(
    const chip_id_t chip, const CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    assert_risc_reset_at_core({(size_t)chip, translate_to_api_coords(chip, core)}, soft_resets);
}

tt_ClusterDescriptor* Cluster::get_cluster_description() { return cluster_desc.get(); }

// Can be used before instantiating a silicon device
int Cluster::detect_number_of_chips() {
    auto available_device_ids = detect_available_device_ids();
    return available_device_ids.size();
}

// Can be used before instantiating a silicon device
std::vector<chip_id_t> Cluster::detect_available_device_ids() {
    // TODO: The chip_id_t type is used for two types of device id:
    //  *   device id which is the N in /dev/tenstorrent/N
    //  *   "logical" id which is the id of the chip in the YAML produced by
    //      the create-ethernet-map tool
    // Maybe these should be disambiguated.  Here, what is being returned is the
    // former, the "device id" -- not to be confused with 16 bit PCI device id!
    return PCIDevice::enumerate_devices();
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> Cluster::get_fast_pcie_static_tlb_write_callable(
    int device_id) {
    TTDevice* dev = get_tt_device(device_id);

    const auto callable = [dev](uint32_t byte_addr, uint32_t num_bytes, const uint8_t* buffer_addr) {
        dev->write_block(byte_addr, num_bytes, buffer_addr);
    };

    return callable;
}

tt::Writer Cluster::get_static_tlb_writer(tt_cxy_pair target) {
    return get_tlb_manager(target.chip)->get_static_tlb_writer({target.x, target.y});
}

tt::Writer Cluster::get_static_tlb_writer(const chip_id_t chip, const CoreCoord target) {
    return get_static_tlb_writer({(size_t)chip, translate_to_api_coords(chip, target)});
}

void Cluster::write_device_memory(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    tt_cxy_pair target,
    uint64_t address,
    const std::string& fallback_tlb) {
    get_local_chip(target.chip)->write_to_device({target.x, target.y}, mem_ptr, address, size_in_bytes, fallback_tlb);
}

void Cluster::read_device_memory(
    void* mem_ptr, tt_cxy_pair target, uint64_t address, uint32_t size_in_bytes, const std::string& fallback_tlb) {
    get_local_chip(target.chip)->read_from_device({target.x, target.y}, mem_ptr, address, size_in_bytes, fallback_tlb);
}

uint32_t Cluster::get_power_state_arc_msg(chip_id_t chip_id, tt_DevicePowerState state) {
    TTDevice* tt_device = get_tt_device(chip_id);
    uint32_t msg = 0xaa00;
    switch (state) {
        case BUSY: {
            msg |= tt_device->get_architecture_implementation()->get_arc_message_arc_go_busy();
            break;
        }
        case LONG_IDLE: {
            msg |= tt_device->get_architecture_implementation()->get_arc_message_arc_go_long_idle();
            break;
        }
        case SHORT_IDLE: {
            msg |= tt_device->get_architecture_implementation()->get_arc_message_arc_go_short_idle();
            break;
        }
        default:
            throw std::runtime_error("Unrecognized power state.");
    }
    return msg;
}

void Cluster::set_pcie_power_state(tt_DevicePowerState state) {
    for (auto& chip_id : local_chip_ids_) {
        uint32_t msg = get_power_state_arc_msg(chip_id, state);
        std::stringstream ss;
        ss << state;
        auto exit_code = arc_msg(chip_id, 0xaa00 | msg, true, 0, 0);
        if (exit_code != 0) {
            throw std::runtime_error(
                fmt::format("Failed to set power state to {} with exit code {}", ss.str(), exit_code));
        }
    }
}

int Cluster::get_clock(int logical_device_id) {
    auto mmio_capable_chip_logical = cluster_desc->get_closest_mmio_capable_chip(logical_device_id);
    return get_tt_device(mmio_capable_chip_logical)->get_clock();
}

std::map<int, int> Cluster::get_clocks() {
    std::map<int, int> clock_freq_map;
    for (auto& chip_id : local_chip_ids_) {
        clock_freq_map.insert({chip_id, get_clock(chip_id)});
    }
    return clock_freq_map;
}

void Cluster::wait_for_aiclk_value(tt_DevicePowerState power_state, const uint32_t timeout_ms) {
    auto start = std::chrono::system_clock::now();
    for (auto& chip_id : local_chip_ids_) {
        uint32_t target_aiclk = 0;
        if (power_state == tt_DevicePowerState::BUSY) {
            target_aiclk = get_tt_device(chip_id)->get_max_clock_freq();
        } else if (power_state == tt_DevicePowerState::LONG_IDLE) {
            target_aiclk = get_tt_device(chip_id)->get_min_clock_freq();
        }
        uint32_t aiclk = get_clock(chip_id);
        while (aiclk != target_aiclk) {
            auto end = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            if (duration.count() > timeout_ms) {
                throw std::runtime_error(
                    fmt::format("Waiting for AICLK value to settle failed on timeout after {}.", timeout_ms));
            }
            aiclk = get_clock(chip_id);
        }
    }
}

Cluster::~Cluster() {
    log_debug(LogSiliconDriver, "Cluster::~Cluster");

    cluster_desc.reset();
}

std::optional<std::tuple<uint32_t, uint32_t>> Cluster::get_tlb_data_from_target(const tt_cxy_pair& target) {
    tlb_configuration tlb_configuration = get_tlb_configuration(target);
    return std::tuple((uint32_t)tlb_configuration.tlb_offset, (uint32_t)tlb_configuration.size);
}

tlb_configuration Cluster::get_tlb_configuration(const tt_cxy_pair& target) {
    return get_tlb_manager(target.chip)->get_tlb_configuration({target.x, target.y});
}

std::optional<std::tuple<uint32_t, uint32_t>> Cluster::get_tlb_data_from_target(const chip_id_t chip, CoreCoord core) {
    return get_tlb_data_from_target({(size_t)chip, translate_to_api_coords(chip, core)});
}

tlb_configuration Cluster::get_tlb_configuration(const chip_id_t chip, CoreCoord core) {
    return get_tlb_configuration({(size_t)chip, translate_to_api_coords(chip, core)});
}

void Cluster::configure_tlb(
    chip_id_t logical_device_id, tt_xy_pair core, int32_t tlb_index, uint64_t address, uint64_t ordering) {
    get_tlb_manager(logical_device_id)
        ->configure_tlb(
            core, translate_chip_coord_virtual_to_translated(logical_device_id, core), tlb_index, address, ordering);
}

void Cluster::configure_tlb(
    chip_id_t logical_device_id, tt::umd::CoreCoord core, int32_t tlb_index, uint64_t address, uint64_t ordering) {
    configure_tlb(logical_device_id, translate_to_api_coords(logical_device_id, core), tlb_index, address, ordering);
}

// TODO: this is in the wrong place, it should be in the TTDevice.
// It should also happen at the same time the huge pages or sysmem buffers are
// allocated/pinned/mapped.
void Cluster::init_pcie_iatus() {
    int num_enabled_devices = local_chip_ids_.size();
    log_debug(LogSiliconDriver, "Cluster::init_pcie_iatus() num_enabled_devices: {}", num_enabled_devices);

    for (auto& chip_id : local_chip_ids_) {
        TTDevice* tt_device = get_tt_device(chip_id);

        // TODO: with the IOMMU case, I think we can get away with using just
        // one iATU region for WH.  (On BH, we don't need iATU).  We can only
        // cover slightly less than 4GB with WH, and the iATU can cover 4GB.
        // Splitting it into multiple regions is fine, but it's not necessary.
        //
        // Update: unfortunately this turned out to be unrealistic.  For the
        // IOMMU case, the easiest thing to do is fake that we have hugepages
        // so we can support the hugepage-inspired API that the user application
        // has come to rely on.  In that scenario, it's simpler to treat such
        // fake hugepages the same way we treat real ones -- even if underneath
        // there is only a single buffer.  Simple is good.
        //
        // With respect to BH: it turns out that Metal has hard-coded NOC
        // addressing assumptions for sysmem access.  First step to fix this is
        // have Metal ask us where sysmem is at runtime, and use that value in
        // on-device code.  Until then, we're stuck programming iATU.  A more
        // forward-looking solution is to abandon the sysmem API entirely, and
        // have the application assume a more active role in managing the memory
        // shared between host and device.  UMD would be relegated to assisting
        // the application set up and tear down the mappings.  This is probably
        // a unrealistic for GS/WH, but it's a good goal for BH.
        //
        // Until then...
        //
        // For every 1GB channel of memory mapped for DMA, program an iATU
        // region to map it to the underlying buffer's IOVA (IOMMU case) or PA
        // (non-IOMMU case).
        for (size_t channel = 0; channel < get_local_chip(chip_id)->get_sysmem_manager()->get_num_host_mem_channels();
             channel++) {
            hugepage_mapping hugepage_map =
                get_local_chip(chip_id)->get_sysmem_manager()->get_hugepage_mapping(channel);
            size_t region_size = hugepage_map.mapping_size;

            if (!hugepage_map.mapping) {
                throw std::runtime_error(
                    fmt::format("Hugepages are not allocated for logical device id: {} ch: {}", chip_id, channel));
            }

            if (arch_name == tt::ARCH::BLACKHOLE) {
                uint64_t base = channel * region_size;
                uint64_t target = hugepage_map.physical_address;
                tt_device->configure_iatu_region(channel, base, target, region_size);
            } else {
                // TODO: stop doing this.  The intent was good, but it's not
                // documented and nothing takes advantage of it.
                if (channel == 3) {
                    region_size = HUGEPAGE_CHANNEL_3_SIZE_LIMIT;
                }

                // TODO: remove this and the Blackhole special case after ARC
                // messaging is lowered to the TTDevice layer and we have a
                // configure_iatu_region that works for GS/WH.  Longer term it'd
                // be nice to have KMD deal with iATU for us...
                iatu_configure_peer_region(chip_id, channel, hugepage_map.physical_address, region_size);
            }
        }
    }
}

int Cluster::test_setup_interface() {
    int ret_val = 0;
    int chip_id = *local_chip_ids_.begin();
    TTDevice* tt_device = get_tt_device(chip_id);
    if (arch_name == tt::ARCH::WORMHOLE_B0) {
        uint32_t mapped_reg = tt_device
                                  ->set_dynamic_tlb(
                                      tt_device->get_architecture_implementation()->get_reg_tlb(),
                                      translate_chip_coord_virtual_to_translated(chip_id, tt_xy_pair(1, 0)),
                                      0xffb20108)
                                  .bar_offset;

        uint32_t regval = 0;
        tt_device->read_regs(mapped_reg, 1, &regval);
        ret_val = (regval != 0xffffffff && (regval == 33)) ? 0 : 1;
        return ret_val;
    } else if (arch_name == tt::ARCH::BLACKHOLE) {
        // MT Inital BH - Try to enable this, but double check "regval == 33"

        // uint32_t mapped_reg = tt_device
        //                           ->set_dynamic_tlb(
        //                               tt_device->get_architecture_implementation()->get_reg_tlb(),
        //                               translate_chip_coord_virtual_to_translated(chip_id, tt_xy_pair(1, 0)),
        //                               0xffb20108)
        //                           .bar_offset;

        // uint32_t regval = 0;
        // tt_device->read_regs(dev, mapped_reg, 1, &regval);
        // ret_val = (regval != 0xffffffff && (regval == 33)) ? 0 : 1;
        // return ret_val;
        return 0;
    } else {
        throw std::runtime_error(fmt::format("Unsupported architecture: {}", arch_to_str(arch_name)));
    }
}

void Cluster::bar_write32(int logical_device_id, uint32_t addr, uint32_t data) {
    TTDevice* dev = get_tt_device(logical_device_id);

    if (addr < dev->get_pci_device()->bar0_uc_offset) {
        dev->write_block(
            addr, sizeof(data), reinterpret_cast<const uint8_t*>(&data));  // do we have to reinterpret_cast?
    } else {
        dev->write_regs(addr, 1, &data);
    }
}

uint32_t Cluster::bar_read32(int logical_device_id, uint32_t addr) {
    TTDevice* dev = get_tt_device(logical_device_id);

    uint32_t data;
    if (addr < dev->get_pci_device()->bar0_uc_offset) {
        dev->read_block(addr, sizeof(data), reinterpret_cast<uint8_t*>(&data));
    } else {
        dev->read_regs(addr, 1, &data);
    }
    return data;
}

// Returns 0 if everything was OK
int Cluster::pcie_arc_msg(
    int logical_device_id,
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    std::vector<uint32_t> arc_msg_return_values;
    if (return_3 != nullptr) {
        arc_msg_return_values.push_back(0);
    }

    if (return_4 != nullptr) {
        arc_msg_return_values.push_back(0);
    }

    uint32_t exit_code = get_tt_device(logical_device_id)
                             ->get_arc_messenger()
                             ->send_message(msg_code, arc_msg_return_values, arg0, arg1, timeout_ms);

    if (return_3 != nullptr) {
        *return_3 = arc_msg_return_values[0];
    }

    if (return_4 != nullptr) {
        *return_4 = arc_msg_return_values[1];
    }

    return exit_code;
}

// TODO: this method should be lowered into TTDevice, where a common
// implementation can be shared between GS/WH.  The major obstacle to doing it
// (and the reason I'm leaving it alone for now) is the lack of ARC messaging
// support at that layer of abstraction.
int Cluster::iatu_configure_peer_region(
    int logical_device_id, uint32_t peer_region_id, uint64_t bar_addr_64, uint32_t region_size) {
    if (arch_name == tt::ARCH::BLACKHOLE) {
        throw std::runtime_error("Don't call this for Blackhole");
    }

    uint32_t dest_bar_lo = bar_addr_64 & 0xffffffff;
    uint32_t dest_bar_hi = (bar_addr_64 >> 32) & 0xffffffff;
    std::uint32_t region_id_to_use = peer_region_id;

    // TODO: stop doing this.  It's related to HUGEPAGE_CHANNEL_3_SIZE_LIMIT.
    if (peer_region_id == 3) {
        region_id_to_use = 4;  // Hack use region 4 for channel 3..this ensures that we have a smaller chan 3 address
                               // space with the correct start offset
    }

    TTDevice* tt_device = get_tt_device(logical_device_id);
    PCIDevice* pci_device = tt_device->get_pci_device();
    auto architecture_implementation = tt_device->get_architecture_implementation();

    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 0 * 4, region_id_to_use);
    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 1 * 4, dest_bar_lo);
    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 2 * 4, dest_bar_hi);
    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 3 * 4, region_size);
    arc_msg(
        logical_device_id,
        0xaa00 | architecture_implementation->get_arc_message_setup_iatu_for_peer_to_peer(),
        true,
        0,
        0);

    // Print what just happened
    uint32_t peer_region_start = region_id_to_use * region_size;
    uint32_t peer_region_end = (region_id_to_use + 1) * region_size - 1;
    log_debug(
        LogSiliconDriver,
        "    [region id {}] NOC to PCI address range 0x{:x}-0x{:x} mapped to addr 0x{:x}",
        peer_region_id,
        peer_region_start,
        peer_region_end,
        bar_addr_64);
    return 0;
}

// Returns broken rows as bits set to 1 in 'memory' and 'logic'
uint32_t Cluster::get_harvested_noc_rows(uint32_t harvesting_mask) {
    auto architecture_implementation = tt::umd::architecture_implementation::create(arch_name);
    const std::vector<uint32_t>& harv_to_noc_loc = architecture_implementation->get_harvesting_noc_locations();
    uint32_t harv_noc_rows = 0;
    std::string harv_noc_rows_str = "";

    for (int pos = 0; pos < harv_to_noc_loc.size(); ++pos) {
        bool is_row_harvested = harvesting_mask & 0x1;
        if (is_row_harvested) {
            harv_noc_rows |= (1 << harv_to_noc_loc[pos]);
            if (harv_noc_rows_str != "") {
                harv_noc_rows_str += ", ";
            }
            harv_noc_rows_str += std::to_string(harv_to_noc_loc[pos]);
        }
        harvesting_mask = harvesting_mask >> 1;
    }
    if (harv_noc_rows > 0) {
        log_debug(LogSiliconDriver, "HARVESTING NOC Y-LOC 0x{:x} = {{}}", harv_noc_rows, harv_noc_rows_str.c_str());
    }
    return harv_noc_rows;
}

uint32_t Cluster::get_harvested_rows(int logical_device_id) {
    const char* harv_override = std::getenv("T6PY_HARVESTING_OVERRIDE");
    uint32_t harv = 0xffffffff;
    if (harv_override) {
        harv = std::stoul(harv_override, nullptr, 16);
    } else {
        auto mmio_capable_chip_logical = cluster_desc->get_closest_mmio_capable_chip(logical_device_id);
        TTDevice* tt_device = get_tt_device(mmio_capable_chip_logical);
        int harvesting_msg_code = arc_msg(
            logical_device_id,
            0xaa00 | tt_device->get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
            true,
            0,
            0,
            1000,
            &harv);
        log_assert(
            harvesting_msg_code != MSG_ERROR_REPLY, "Failed to read harvested rows from device {}", logical_device_id);
    }
    log_assert(harv != 0xffffffff, "Readback 0xffffffff for harvesting info. Chip is fused incorrectly!");
    log_debug(LogSiliconDriver, "HARVESTING {}, 0x{:x}", (harv == 0) ? "DISABLED" : "ENABLED", harv);

    uint32_t memory = harv & 0x3ff;
    uint32_t logic = (harv >> 10) & 0x3ff;
    return (memory | logic);
}

uint32_t Cluster::get_harvested_noc_rows_for_chip(int logical_device_id) {
    return get_harvested_noc_rows(get_harvested_rows(logical_device_id));
}

void Cluster::enable_local_ethernet_queue(const chip_id_t& device_id, int timeout) {
    uint32_t msg_success = 0x0;
    auto timeout_seconds = std::chrono::seconds(timeout);
    auto start = std::chrono::system_clock::now();
    while (msg_success != 1) {
        if (std::chrono::system_clock::now() - start > timeout_seconds) {
            throw std::runtime_error(
                fmt::format("Timed out after waiting {} seconds for for DRAM to finish training", timeout));
        }

        if (arc_msg(device_id, 0xaa58, true, 0xFFFF, 0xFFFF, 1000, &msg_success) == MSG_ERROR_REPLY) {
            break;
        }
    }
}

void* Cluster::host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
    hugepage_mapping hugepage_map = get_local_chip(src_device_id)->get_sysmem_manager()->get_hugepage_mapping(channel);
    if (hugepage_map.mapping != nullptr) {
        return static_cast<std::byte*>(hugepage_map.mapping) + offset;
    } else {
        return nullptr;
    }
}

inline TTDevice* Cluster::get_tt_device(chip_id_t device_id) const {
    auto tt_device = get_local_chip(device_id)->get_tt_device();
    log_assert(tt_device != nullptr, "TTDevice not found for device: {}", device_id);
    return tt_device;
}

inline TLBManager* Cluster::get_tlb_manager(chip_id_t device_id) const {
    return get_local_chip(device_id)->get_tlb_manager();
}

inline Chip* Cluster::get_chip(chip_id_t device_id) const {
    auto chip_it = chips_.find(device_id);
    log_assert(chip_it != chips_.end(), "Device id {} not found in cluster.", device_id);
    return chip_it->second.get();
}

inline Chip* Cluster::get_local_chip(chip_id_t device_id) const {
    log_assert(
        local_chip_ids_.find(device_id) != local_chip_ids_.end(), "Device id {} is not a local chip.", device_id);
    return get_chip(device_id);
}

/*
 *
 *                                       NON_MMIO_MUTEX Usage
 *
 * Relevant functions:
 *  - write_to_non_mmio_device
 *  - read_from_non_mmio_device
 *
 * The non-MMIO read/write functions (excluding the `*_epoch_cmd` variants) are responsible for the
 * writes/reads to/from those wormhole chips that aren't memory mapped or directly host connected.
 * To get the data to or from those other chips, there is a memory transfer protocol - initiated on
 * the host side but carried out by any number of the ethernet cores (the ethernet core pool is dictated
 * by `this->NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS`) on the MMIO chips (e.g. typically just the one chip in a galaxy).
 *
 * There is a command queue structure in ethernet core FW to accept these read/write commands. However, there is no
 * atomic increment (from host side) for the write pointers of these queues, nor is there any sort of other hardware
 * mutual exclusion (as of WH) from host side when populating commands into the queue (as in when the host pushes a
 * write command into the ethernet core's queue).
 *
 * Therefore, any of these non_mmio commands from host side need to be synchronized so they don't accidentally corrupt
 * each other. The finest granularity possible to synchronize on would be the command slot and wrptr (per core),
 * but wrptr updates also need to be coordinated:
 *  - you can't increment wrptr unless you are writing to the next index and your write is complete
 *  - if two threads could guarantee separate command slots, they'd need to order their wrptr updates from lowest to
 *    highest and based on completion of command writes.
 *
 * Stepping back a little bit, a sort of interprocess synchronization is required because the driver may be invoked
 * from several processes. Indeed from pybuda (python), we'd typically needs to spin up multiple processes:
 *   - 1 for pushing inputs
 *   - 1 for popping outputs
 *   - 1 for managing execution state
 *  (or some variation along those lines).
 *
 * The interprocess mutex from measurements takes a while. While not seconds, it's non-trivial such that locking and
 * unlocking at fine granularity would be more detrimental to performance than acquiring it for a large block.
 *
 * Considering the above, the current chosen approach is to make each of these calls acquired a shared mutex:
 * `NON_MMIO_MUTEX_NAME`
 *  - They acquire at a relatively large granularity -> for the entire duration of the function where we interact
 *    with the ethernet core (read/write) and where we use `active_core` to choose a core.
 *    - Simplifies synchronization while we reach stability
 *  - We need to include any usage (read/modify) of `active_core` in the mutex acquisition scope.
 *
 * Other schemes may be more performant.
 */

/*
 * Note that this function is required to acquire the `NON_MMIO_MUTEX_NAME` mutex for interacting with the
 * ethernet core (host) command queue DO NOT issue any pcie reads/writes to the ethernet core prior to acquiring the
 * mutex. For extra information, see the "NON_MMIO_MUTEX Usage" above
 */

void Cluster::write_to_non_mmio_device(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    tt_cxy_pair core,
    uint64_t address,
    bool broadcast,
    std::vector<int> broadcast_header) {
    chip_id_t mmio_capable_chip_logical;

    if (broadcast) {
        mmio_capable_chip_logical = core.chip;
    } else {
        mmio_capable_chip_logical = cluster_desc->get_closest_mmio_capable_chip(core.chip);
    }
    flush_non_mmio_per_chip[cluster_desc->get_closest_mmio_capable_chip(core.chip)] = true;

    if (non_mmio_transfer_cores_customized) {
        log_assert(
            active_eth_core_idx_per_chip.find(mmio_capable_chip_logical) != active_eth_core_idx_per_chip.end(),
            "Ethernet Cores for Host to Cluster communication were not initialized for all MMIO devices.");
    }

    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);
    constexpr int BROADCAST_HEADER_SIZE = sizeof(data_word_t) * 8;  // Broadcast header is 8 words
    const auto target_chip = cluster_desc->get_chip_locations().at(core.chip);

    // TODO: To be removed when this is moved to Chip classes.
    auto host_address_params = get_local_chip(mmio_capable_chip_logical)->host_address_params;
    auto eth_interface_params = get_local_chip(mmio_capable_chip_logical)->eth_interface_params;
    auto noc_params = get_local_chip(mmio_capable_chip_logical)->noc_params;

    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";

    tt_xy_pair translated_core = translate_chip_coord_virtual_to_translated(core.chip, core);
    core.x = translated_core.x;
    core.y = translated_core.y;

    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_q_ptrs =
        std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes * 2 / sizeof(uint32_t));

    std::vector<std::uint32_t> data_block;

    routing_cmd_t* new_cmd;

    uint32_t buffer_id = 0;
    uint32_t timestamp = 0;  // CMD_TIMESTAMP;
    bool use_dram;
    uint32_t max_block_size;

    // Broadcast requires block writes to host dram
    use_dram = broadcast || (size_in_bytes > 256 * DATA_WORD_SIZE);
    max_block_size = use_dram ? host_address_params.eth_routing_block_size : eth_interface_params.max_block_size;

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    auto lock =
        get_local_chip(mmio_capable_chip_logical)
            ->acquire_mutex(
                MutexType::NON_MMIO, get_tt_device(mmio_capable_chip_logical)->get_pci_device()->get_device_num());

    int& active_core_for_txn =
        non_mmio_transfer_cores_customized ? active_eth_core_idx_per_chip.at(mmio_capable_chip_logical) : active_core;
    tt_cxy_pair remote_transfer_ethernet_core =
        remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn];

    erisc_command.resize(sizeof(routing_cmd_t) / DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t*)&erisc_command[0];
    read_device_memory(
        erisc_q_ptrs.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
        eth_interface_params.remote_update_ptr_size_bytes * 2,
        read_tlb);
    uint32_t full_count = 0;
    uint32_t offset = 0;
    uint32_t block_size;

    bool full = is_non_mmio_cmd_q_full(
        chips_.at(mmio_capable_chip_logical)->eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];
    while (offset < size_in_bytes) {
        while (full) {
            read_device_memory(
                erisc_q_rptr.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes +
                    eth_interface_params.remote_update_ptr_size_bytes,
                DATA_WORD_SIZE,
                read_tlb);
            full = is_non_mmio_cmd_q_full(
                chips_.at(mmio_capable_chip_logical)->eth_interface_params, erisc_q_ptrs[0], erisc_q_rptr[0]);
            full_count++;
        }
        // full = true;
        //  set full only if this command will make the q full.
        //  otherwise full stays false so that we do not poll the rd pointer in next iteration.
        //  As long as current command push does not fill up the queue completely, we do not want
        //  to poll rd pointer in every iteration.
        // full = is_non_mmio_cmd_q_full((erisc_q_ptrs[0] + 1) & CMD_BUF_PTR_MASK, erisc_q_rptr[0]);

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.cmd_buf_size_mask;
        if ((address + offset) & 0x1F) {  // address not 32-byte aligned
            block_size = DATA_WORD_SIZE;  // 4 byte aligned
        } else {
            // For broadcast we prepend a 32byte header. Decrease block size (size of payload) by this amount.
            block_size = offset + max_block_size > size_in_bytes + 32 * broadcast ? size_in_bytes - offset
                                                                                  : max_block_size - 32 * broadcast;
            // Explictly align block_size to 4 bytes, in case the input buffer is not uint32_t aligned
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;
        }
        // For 4 byte aligned data, transfer_size always == block_size. For unaligned data, transfer_size < block_size
        // in the last block
        uint64_t transfer_size =
            std::min(block_size, size_in_bytes - offset);  // Host side data size that needs to be copied
        // Use block mode for broadcast
        uint32_t req_flags = (broadcast || (block_size > DATA_WORD_SIZE))
                                 ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_wr_req | timestamp)
                                 : eth_interface_params.cmd_wr_req;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE
                                  ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_wr_ack)
                                  : eth_interface_params.cmd_wr_ack;
        timestamp = 0;

        if (broadcast) {
            req_flags |= eth_interface_params.cmd_broadcast;
        }

        uint32_t host_dram_block_addr =
            host_address_params.eth_routing_buffers_start +
            (active_core_for_txn * eth_interface_params.cmd_buf_size + req_wr_ptr) * max_block_size;
        uint16_t host_dram_channel = 0;  // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (req_flags & eth_interface_params.cmd_data_block) {
            // Copy data to sysmem or device DRAM for Block mode
            if (use_dram) {
                req_flags |= eth_interface_params.cmd_data_block_dram;
                resp_flags |= eth_interface_params.cmd_data_block_dram;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
                if (broadcast) {
                    // Write broadcast header to sysmem
                    write_to_sysmem(
                        broadcast_header.data(),
                        broadcast_header.size() * sizeof(uint32_t),
                        host_dram_block_addr,
                        host_dram_channel,
                        mmio_capable_chip_logical);
                }
                // Write payload to sysmem
                write_to_sysmem(
                    data_block.data(),
                    data_block.size() * DATA_WORD_SIZE,
                    host_dram_block_addr + BROADCAST_HEADER_SIZE * broadcast,
                    host_dram_channel,
                    mmio_capable_chip_logical);

            } else {
                uint32_t buf_address = eth_interface_params.eth_routing_data_buffer_addr + req_wr_ptr * max_block_size;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
                write_device_memory(
                    data_block.data(),
                    data_block.size() * DATA_WORD_SIZE,
                    remote_transfer_ethernet_core,
                    buf_address,
                    write_tlb);
            }
            tt_driver_atomics::sfence();
        }

        // Send the read request
        log_assert(
            broadcast || (req_flags == eth_interface_params.cmd_wr_req) || (((address + offset) % 32) == 0),
            "Block mode address must be 32-byte aligned.");  // Block mode address must be 32-byte aligned.

        if (broadcast) {
            // Only specify endpoint local address for broadcast
            new_cmd->sys_addr = address + offset;
        } else {
            new_cmd->sys_addr =
                get_sys_addr(noc_params, target_chip.x, target_chip.y, core.x, core.y, address + offset);
            new_cmd->rack = get_sys_rack(eth_interface_params, target_chip.rack, target_chip.shelf);
        }

        if (req_flags & eth_interface_params.cmd_data_block) {
            // Block mode
            new_cmd->data = block_size + BROADCAST_HEADER_SIZE * broadcast;
        } else {
            if (size_in_bytes - offset < sizeof(uint32_t)) {
                // Handle misalignment at the end of the buffer:
                // Assemble a padded uint32_t from single bytes, in case we have less than 4 bytes remaining
                memcpy(&new_cmd->data, static_cast<const uint8_t*>(mem_ptr) + offset, size_in_bytes - offset);
            } else {
                new_cmd->data = *((uint32_t*)mem_ptr + offset / DATA_WORD_SIZE);
            }
        }

        new_cmd->flags = req_flags;
        new_cmd->flags |= (umd_use_noc1 ? 1 : 0) << REMOTE_CMD_NOC_BIT;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        write_device_memory(
            erisc_command.data(),
            erisc_command.size() * DATA_WORD_SIZE,
            remote_transfer_ethernet_core,
            eth_interface_params.request_routing_cmd_queue_base + (sizeof(routing_cmd_t) * req_wr_ptr),
            write_tlb);
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        write_device_memory(
            erisc_q_wptr.data(),
            erisc_q_wptr.size() * DATA_WORD_SIZE,
            remote_transfer_ethernet_core,
            eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
            write_tlb);
        tt_driver_atomics::sfence();

        offset += transfer_size;

        // If there is more data to send and this command will make the q full, switch to next Q.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full(
                chips_.at(mmio_capable_chip_logical)->eth_interface_params,
                (erisc_q_ptrs[0]) & eth_interface_params.cmd_buf_ptr_mask,
                erisc_q_rptr[0])) {
            active_core_for_txn++;
            uint32_t update_mask_for_chip = remote_transfer_ethernet_cores[mmio_capable_chip_logical].size() - 1;
            active_core_for_txn =
                non_mmio_transfer_cores_customized
                    ? (active_core_for_txn & update_mask_for_chip)
                    : ((active_core_for_txn & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID);
            // active_core = (active_core & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID;
            remote_transfer_ethernet_core =
                remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn];
            read_device_memory(
                erisc_q_ptrs.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                eth_interface_params.remote_update_ptr_size_bytes * 2,
                read_tlb);
            full = is_non_mmio_cmd_q_full(
                chips_.at(mmio_capable_chip_logical)->eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }
    }
}

/*
 * Note that this function is required to acquire the `NON_MMIO_MUTEX_NAME` mutex for interacting with the ethernet core
 * (host) command queue DO NOT use `active_core` or issue any pcie reads/writes to the ethernet core prior to acquiring
 * the mutex. For extra information, see the "NON_MMIO_MUTEX Usage" above
 */
void Cluster::read_from_non_mmio_device(void* mem_ptr, tt_cxy_pair core, uint64_t address, uint32_t size_in_bytes) {
    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);
    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";

    tt_xy_pair translated_core = translate_chip_coord_virtual_to_translated(core.chip, core);
    core.x = translated_core.x;
    core.y = translated_core.y;

    const auto& mmio_capable_chip_logical = cluster_desc->get_closest_mmio_capable_chip(core.chip);
    const eth_coord_t target_chip = cluster_desc->get_chip_locations().at(core.chip);

    // TODO: To be removed when this is moved to Chip classes.
    auto host_address_params = get_local_chip(mmio_capable_chip_logical)->host_address_params;
    auto eth_interface_params = get_local_chip(mmio_capable_chip_logical)->eth_interface_params;
    auto noc_params = get_local_chip(mmio_capable_chip_logical)->noc_params;

    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr;
    std::vector<std::uint32_t> erisc_q_ptrs =
        std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes * 2 / DATA_WORD_SIZE);
    std::vector<std::uint32_t> erisc_resp_q_wptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_resp_q_rptr = std::vector<uint32_t>(1);

    std::vector<std::uint32_t> data_block;

    routing_cmd_t* new_cmd;

    erisc_command.resize(sizeof(routing_cmd_t) / DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t*)&erisc_command[0];
    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    auto lock =
        get_local_chip(mmio_capable_chip_logical)
            ->acquire_mutex(
                MutexType::NON_MMIO, get_tt_device(mmio_capable_chip_logical)->get_pci_device()->get_device_num());
    const tt_cxy_pair remote_transfer_ethernet_core = remote_transfer_ethernet_cores[mmio_capable_chip_logical].at(0);

    read_device_memory(
        erisc_q_ptrs.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
        eth_interface_params.remote_update_ptr_size_bytes * 2,
        read_tlb);
    read_device_memory(
        erisc_resp_q_wptr.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
        DATA_WORD_SIZE,
        read_tlb);
    read_device_memory(
        erisc_resp_q_rptr.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes +
            eth_interface_params.remote_update_ptr_size_bytes,
        DATA_WORD_SIZE,
        read_tlb);

    bool full = is_non_mmio_cmd_q_full(
        chips_.at(mmio_capable_chip_logical)->eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];

    bool use_dram;
    uint32_t max_block_size;

    use_dram = size_in_bytes > 1024;
    max_block_size = use_dram ? host_address_params.eth_routing_block_size : eth_interface_params.max_block_size;

    uint32_t offset = 0;
    uint32_t block_size;
    uint32_t buffer_id = 0;

    while (offset < size_in_bytes) {
        while (full) {
            read_device_memory(
                erisc_q_rptr.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes +
                    eth_interface_params.remote_update_ptr_size_bytes,
                DATA_WORD_SIZE,
                read_tlb);
            full = is_non_mmio_cmd_q_full(
                chips_.at(mmio_capable_chip_logical)->eth_interface_params, erisc_q_ptrs[0], erisc_q_rptr[0]);
        }

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.cmd_buf_size_mask;
        if ((address + offset) & 0x1F) {  // address not 32-byte aligned
            block_size = DATA_WORD_SIZE;  // 4 byte aligned block
        } else {
            block_size = offset + max_block_size > size_in_bytes ? size_in_bytes - offset : max_block_size;
            // Align up to 4 bytes.
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;
        }
        uint32_t req_flags = block_size > DATA_WORD_SIZE
                                 ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_rd_req)
                                 : eth_interface_params.cmd_rd_req;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE
                                  ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_rd_data)
                                  : eth_interface_params.cmd_rd_data;
        uint32_t resp_rd_ptr = erisc_resp_q_rptr[0] & eth_interface_params.cmd_buf_size_mask;
        uint32_t host_dram_block_addr = host_address_params.eth_routing_buffers_start + resp_rd_ptr * max_block_size;
        uint16_t host_dram_channel = 0;  // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (use_dram && block_size > DATA_WORD_SIZE) {
            req_flags |= eth_interface_params.cmd_data_block_dram;
            resp_flags |= eth_interface_params.cmd_data_block_dram;
        }

        // Send the read request
        log_assert(
            (req_flags == eth_interface_params.cmd_rd_req) || (((address + offset) & 0x1F) == 0),
            "Block mode offset must be 32-byte aligned.");  // Block mode offset must be 32-byte aligned.
        new_cmd->sys_addr = get_sys_addr(noc_params, target_chip.x, target_chip.y, core.x, core.y, address + offset);
        new_cmd->rack = get_sys_rack(eth_interface_params, target_chip.rack, target_chip.shelf);
        new_cmd->data = block_size;
        new_cmd->flags = req_flags;
        new_cmd->flags |= (umd_use_noc1 ? 1 : 0) << REMOTE_CMD_NOC_BIT;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        write_device_memory(
            erisc_command.data(),
            erisc_command.size() * DATA_WORD_SIZE,
            remote_transfer_ethernet_core,
            eth_interface_params.request_routing_cmd_queue_base + (sizeof(routing_cmd_t) * req_wr_ptr),
            write_tlb);
        ;
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        write_device_memory(
            erisc_q_wptr.data(),
            erisc_q_wptr.size() * DATA_WORD_SIZE,
            remote_transfer_ethernet_core,
            eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
            write_tlb);
        tt_driver_atomics::sfence();
        // If there is more data to read and this command will make the q full, set full to 1.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full(
                chips_.at(mmio_capable_chip_logical)->eth_interface_params, (erisc_q_ptrs[0]), erisc_q_rptr[0])) {
            read_device_memory(
                erisc_q_ptrs.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                eth_interface_params.remote_update_ptr_size_bytes * 2,
                read_tlb);
            full = is_non_mmio_cmd_q_full(
                chips_.at(mmio_capable_chip_logical)->eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }

        // Wait for read request completion and extract the data into the `mem_ptr`

        // erisc firmware will:
        // 1. clear response flags
        // 2. start operation
        // 3. advance response wrptr
        // 4. complete operation and write data into response or buffer
        // 5. set response flags
        // So we have to wait for wrptr to advance, then wait for flags to be nonzero, then read data.

        do {
            read_device_memory(
                erisc_resp_q_wptr.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                DATA_WORD_SIZE,
                read_tlb);
        } while (erisc_resp_q_rptr[0] == erisc_resp_q_wptr[0]);
        tt_driver_atomics::lfence();
        uint32_t flags_offset = 12 + sizeof(routing_cmd_t) * resp_rd_ptr;
        std::vector<std::uint32_t> erisc_resp_flags = std::vector<uint32_t>(1);
        do {
            read_device_memory(
                erisc_resp_flags.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.response_routing_cmd_queue_base + flags_offset,
                DATA_WORD_SIZE,
                read_tlb);
        } while (erisc_resp_flags[0] == 0);

        if (erisc_resp_flags[0] == resp_flags) {
            tt_driver_atomics::lfence();
            uint32_t data_offset = 8 + sizeof(routing_cmd_t) * resp_rd_ptr;
            if (block_size == DATA_WORD_SIZE) {
                std::vector<std::uint32_t> erisc_resp_data = std::vector<uint32_t>(1);
                read_device_memory(
                    erisc_resp_data.data(),
                    remote_transfer_ethernet_core,
                    eth_interface_params.response_routing_cmd_queue_base + data_offset,
                    DATA_WORD_SIZE,
                    read_tlb);
                if (size_in_bytes - offset < 4) {
                    // Handle misaligned (4 bytes) data at the end of the block.
                    // Only read remaining bytes into the host buffer, instead of reading the full uint32_t
                    std::memcpy((uint8_t*)mem_ptr + offset, erisc_resp_data.data(), size_in_bytes - offset);
                } else {
                    *((uint32_t*)mem_ptr + offset / DATA_WORD_SIZE) = erisc_resp_data[0];
                }
            } else {
                // Read 4 byte aligned block from device/sysmem
                if (use_dram) {
                    size_buffer_to_capacity(data_block, block_size);
                    read_from_sysmem(
                        data_block.data(),
                        host_dram_block_addr,
                        host_dram_channel,
                        block_size,
                        mmio_capable_chip_logical);
                } else {
                    uint32_t buf_address =
                        eth_interface_params.eth_routing_data_buffer_addr + resp_rd_ptr * max_block_size;
                    size_buffer_to_capacity(data_block, block_size);
                    read_device_memory(
                        data_block.data(), remote_transfer_ethernet_core, buf_address, block_size, read_tlb);
                }
                // assert(mem_ptr.size() - (offset/DATA_WORD_SIZE) >= (block_size * DATA_WORD_SIZE));
                log_assert(
                    (data_block.size() * DATA_WORD_SIZE) >= block_size,
                    "Incorrect data size read back from sysmem/device");
                // Account for misalignment by skipping any padding bytes in the copied data_block
                memcpy((uint8_t*)mem_ptr + offset, data_block.data(), std::min(block_size, size_in_bytes - offset));
            }
        }

        // Finally increment the rdptr for the response command q
        erisc_resp_q_rptr[0] = (erisc_resp_q_rptr[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        write_device_memory(
            erisc_resp_q_rptr.data(),
            erisc_resp_q_rptr.size() * DATA_WORD_SIZE,
            remote_transfer_ethernet_core,
            eth_interface_params.response_cmd_queue_base + sizeof(remote_update_ptr_t) +
                eth_interface_params.cmd_counters_size_bytes,
            write_tlb);
        tt_driver_atomics::sfence();
        log_assert(erisc_resp_flags[0] == resp_flags, "Unexpected ERISC Response Flags.");

        offset += block_size;
    }
}

void Cluster::wait_for_connected_non_mmio_flush(const chip_id_t chip_id) {
    if (flush_non_mmio_per_chip[chip_id]) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");
        std::string read_tlb = "LARGE_READ_TLB";
        auto chips_with_mmio = this->get_target_mmio_device_ids();

        if (chips_with_mmio.find(chip_id) == chips_with_mmio.end()) {
            log_debug(
                LogSiliconDriver, "Chip {} is not an MMIO chip, skipping wait_for_connected_non_mmio_flush", chip_id);
            return;
        }

        if (arch_name == tt::ARCH::WORMHOLE_B0) {
            // TODO: To be removed when this is moved to Chip classes.
            auto eth_interface_params = get_chip(chip_id)->eth_interface_params;

            std::vector<std::uint32_t> erisc_txn_counters = std::vector<uint32_t>(2);
            std::vector<std::uint32_t> erisc_q_ptrs =
                std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes * 2 / sizeof(uint32_t));

            // wait for all queues to be empty.
            for (tt_cxy_pair& cxy : remote_transfer_ethernet_cores.at(chip_id)) {
                do {
                    read_device_memory(
                        erisc_q_ptrs.data(),
                        cxy,
                        eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                        eth_interface_params.remote_update_ptr_size_bytes * 2,
                        read_tlb);
                } while (erisc_q_ptrs[0] != erisc_q_ptrs[4]);
            }
            // wait for all write responses to come back.
            for (tt_cxy_pair& cxy : remote_transfer_ethernet_cores.at(chip_id)) {
                do {
                    read_device_memory(
                        erisc_txn_counters.data(), cxy, eth_interface_params.request_cmd_queue_base, 8, read_tlb);
                } while (erisc_txn_counters[0] != erisc_txn_counters[1]);
            }
        }
        flush_non_mmio_per_chip[chip_id] = false;
    }
}

void Cluster::wait_for_non_mmio_flush(const chip_id_t chip_id) {
    if (!this->cluster_desc->is_chip_remote(chip_id)) {
        log_debug(LogSiliconDriver, "Chip {} is not a remote chip, skipping wait_for_non_mmio_flush", chip_id);
        return;
    }

    std::string read_tlb = "LARGE_READ_TLB";
    log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");

    chip_id_t mmio_connected_chip = cluster_desc->get_closest_mmio_capable_chip(chip_id);
    wait_for_connected_non_mmio_flush(mmio_connected_chip);
}

void Cluster::wait_for_non_mmio_flush() {
    for (auto& chip_id : get_target_mmio_device_ids()) {
        wait_for_connected_non_mmio_flush(chip_id);
    }
}

// Broadcast Functions
void Cluster::generate_tensix_broadcast_grids_for_grayskull(
    std::set<std::pair<tt_xy_pair, tt_xy_pair>>& broadcast_grids,
    std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& cols_to_exclude) {
    // If row 0 is not explicitly excluded, exclude it here since its non-tensix
    rows_to_exclude.insert(0);
    // If row 11 is excluded, we can close the SOC grid. If not, exclude row 12 to close grid.
    if (rows_to_exclude.find(11) == rows_to_exclude.end()) {
        rows_to_exclude.insert(12);
    }
    // If col 0 is not explicitly excluded, exclude it here since its non-tensix
    cols_to_exclude.insert(0);
    // If col 12 is excluded, we can close the SOC grid. If not, exclude col 13 to close grid.
    if (cols_to_exclude.find(12) == cols_to_exclude.end()) {
        cols_to_exclude.insert(13);
    }
    std::vector<std::pair<int, int>> bb_x_coords = {};
    std::vector<std::pair<int, int>> bb_y_coords = {};

    // Generate starting and ending x coordinates of each bounding box/grid
    for (auto x_it = cols_to_exclude.begin(); x_it != cols_to_exclude.end(); x_it++) {
        if (x_it == std::prev(cols_to_exclude.end(), 1)) {
            continue;
        }
        if (cols_to_exclude.find(*(x_it) + 1) == cols_to_exclude.end() and
            cols_to_exclude.find(*(std::next(x_it, 1)) - 1) == cols_to_exclude.end()) {
            bb_x_coords.push_back({*(x_it) + 1, *(std::next(x_it, 1)) - 1});
        }
    }

    for (auto y_it = rows_to_exclude.begin(); y_it != rows_to_exclude.end(); y_it++) {
        if (y_it == std::prev(rows_to_exclude.end(), 1)) {
            continue;
        }
        if (rows_to_exclude.find((*y_it) + 1) == rows_to_exclude.end() and
            rows_to_exclude.find(*std::next(y_it, 1) - 1) == rows_to_exclude.end()) {
            bb_y_coords.push_back({*(y_it) + 1, *(std::next(y_it, 1)) - 1});
        }
    }
    // Assemble x and y coordinates into bounding box vertices
    for (const auto& x_pair : bb_x_coords) {
        for (const auto& y_pair : bb_y_coords) {
            tt_xy_pair top_left = tt_xy_pair(x_pair.first, y_pair.first);
            tt_xy_pair bot_right = tt_xy_pair(x_pair.second, y_pair.second);
            broadcast_grids.insert({top_left, bot_right});
        }
    }
}

std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& Cluster::get_ethernet_broadcast_headers(
    const std::set<chip_id_t>& chips_to_exclude) {
    // Generate headers for Ethernet Broadcast (WH) only. Each header corresponds to a unique broadcast "grid".
    if (bcast_header_cache.find(chips_to_exclude) == bcast_header_cache.end()) {
        bcast_header_cache[chips_to_exclude] = {};
        std::unordered_map<chip_id_t, std::unordered_map<chip_id_t, std::vector<int>>>
            broadcast_mask_for_target_chips_per_group = {};
        std::map<std::vector<int>, std::tuple<chip_id_t, std::vector<int>>> broadcast_header_union_per_group = {};
        chip_id_t first_mmio_chip = *(get_target_mmio_device_ids().begin());
        for (const auto& chip : all_chip_ids_) {
            if (chips_to_exclude.find(chip) == chips_to_exclude.end()) {
                // Get shelf local physical chip id included in broadcast
                chip_id_t physical_chip_id = cluster_desc->get_shelf_local_physical_chip_coords(chip);
                eth_coord_t eth_coords = cluster_desc->get_chip_locations().at(chip);
                // Rack word to be set in header
                uint32_t rack_word = eth_coords.rack >> 2;
                // Rack byte to be set in header
                uint32_t rack_byte = eth_coords.rack % 4;
                // 1st level grouping: Group broadcasts based on the MMIO chip they must go through
                // Nebula + Galaxy Topology assumption: Disjoint sets can only be present in the first shelf, with each
                // set connected to host through its closest MMIO chip For the first shelf, pass broadcasts to specific
                // chips through their closest MMIO chip All other shelves are fully connected galaxy grids. These are
                // connected to all MMIO devices. Use any (or the first) MMIO device in the list.
                chip_id_t closest_mmio_chip = 0;
                if (eth_coords.rack == 0 && eth_coords.shelf == 0) {
                    // Shelf 0 + Rack 0: Either an MMIO chip or a remote chip potentially connected to host through its
                    // own MMIO counterpart.
                    closest_mmio_chip = cluster_desc->get_closest_mmio_capable_chip(chip);
                } else {
                    // All other shelves: Group these under the same/first MMIO chip, since all MMIO chips are
                    // connected.
                    closest_mmio_chip = first_mmio_chip;
                }
                if (broadcast_mask_for_target_chips_per_group.find(closest_mmio_chip) ==
                    broadcast_mask_for_target_chips_per_group.end()) {
                    broadcast_mask_for_target_chips_per_group.insert({closest_mmio_chip, {}});
                }
                // For each target physical chip id (local to a shelf), generate headers based on all racks and shelves
                // that contain this physical id.
                if (broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).find(physical_chip_id) ==
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).end()) {
                    // Target seen for the first time.
                    std::vector<int> broadcast_mask(8, 0);
                    broadcast_mask.at(rack_word) |= (1 << eth_coords.shelf) << rack_byte;
                    broadcast_mask.at(3) |= 1 << physical_chip_id;
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip)
                        .insert({physical_chip_id, broadcast_mask});

                } else {
                    // Target was seen before -> include curr rack and shelf in header
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip)
                        .at(physical_chip_id)
                        .at(rack_word) |= static_cast<uint32_t>(1 << eth_coords.shelf) << rack_byte;
                }
            }
        }
        // 2nd level grouping: For each MMIO group, further group the chips based on their rack and shelf headers. The
        // number of groups after this step represent the final set of broadcast grids.
        for (auto& mmio_group : broadcast_mask_for_target_chips_per_group) {
            for (auto& chip : mmio_group.second) {
                // Generate a hash for this MMIO Chip + Rack + Shelf group
                std::vector<int> header_hash = {
                    mmio_group.first, chip.second.at(0), chip.second.at(1), chip.second.at(2)};
                if (broadcast_header_union_per_group.find(header_hash) == broadcast_header_union_per_group.end()) {
                    broadcast_header_union_per_group.insert(
                        {header_hash, std::make_tuple(mmio_group.first, chip.second)});
                } else {
                    // If group found, update chip header entry
                    std::get<1>(broadcast_header_union_per_group.at(header_hash)).at(3) |= chip.second.at(3);
                }
            }
        }
        // Get all broadcast headers per MMIO group
        for (const auto& header : broadcast_header_union_per_group) {
            chip_id_t mmio_chip = std::get<0>(header.second);
            if (bcast_header_cache[chips_to_exclude].find(mmio_chip) == bcast_header_cache[chips_to_exclude].end()) {
                bcast_header_cache[chips_to_exclude].insert({mmio_chip, {}});
            }
            bcast_header_cache[chips_to_exclude].at(mmio_chip).push_back(std::get<1>(header.second));
        }
        // Invert headers (FW convention)
        for (auto& bcast_group : bcast_header_cache[chips_to_exclude]) {
            for (auto& header : bcast_group.second) {
                int header_idx = 0;
                for (auto& header_entry : header) {
                    if (header_idx == 4) {
                        break;
                    }
                    header_entry = ~header_entry;
                    header_idx++;
                }
            }
        }
    }
    return bcast_header_cache[chips_to_exclude];
}

inline bool tensix_or_eth_in_broadcast(
    const std::set<uint32_t>& cols_to_exclude,
    const tt::umd::architecture_implementation* architecture_implementation) {
    bool found_tensix_or_eth = false;
    for (const auto& col : architecture_implementation->get_t6_x_locations()) {
        found_tensix_or_eth |= (cols_to_exclude.find(col) == cols_to_exclude.end());
    }
    return found_tensix_or_eth;
}

inline bool valid_tensix_broadcast_grid(
    const std::set<uint32_t>& rows_to_exclude,
    const std::set<uint32_t>& cols_to_exclude,
    const tt::umd::architecture_implementation* architecture_implementation) {
    bool t6_bcast_rows_complete = true;
    bool t6_bcast_rows_empty = true;

    for (const auto& row : architecture_implementation->get_t6_y_locations()) {
        t6_bcast_rows_complete &= (rows_to_exclude.find(row) == rows_to_exclude.end());
        t6_bcast_rows_empty &= (rows_to_exclude.find(row) != rows_to_exclude.end());
    }
    return t6_bcast_rows_complete || t6_bcast_rows_empty;
}

void Cluster::ethernet_broadcast_write(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<chip_id_t>& chips_to_exclude,
    const std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& cols_to_exclude,
    const std::string& fallback_tlb,
    bool use_virtual_coords) {
    if (use_ethernet_broadcast) {
        // Broadcast through ERISC core supported
        std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& broadcast_headers =
            get_ethernet_broadcast_headers(chips_to_exclude);
        // Apply row and column exclusion mask explictly. Placing this here if we want to cache the higher level
        // broadcast headers on future/
        std::uint32_t row_exclusion_mask = 0;
        std::uint32_t col_exclusion_mask = 0;
        for (const auto& row : rows_to_exclude) {
            row_exclusion_mask |= 1 << row;
        }

        for (const auto& col : cols_to_exclude) {
            col_exclusion_mask |= 1 << (16 + col);
        }
        // Write broadcast block to device.
        for (auto& mmio_group : broadcast_headers) {
            for (auto& header : mmio_group.second) {
                header.at(4) = use_virtual_coords * 0x8000;  // Reset row/col exclusion masks
                header.at(4) |= row_exclusion_mask;
                header.at(4) |= col_exclusion_mask;
                // Write Target: x-y endpoint is a don't care. Initialize to tt_xy_pair(1, 1)
                write_to_non_mmio_device(
                    mem_ptr, size_in_bytes, tt_cxy_pair(mmio_group.first, tt_xy_pair(1, 1)), address, true, header);
            }
        }
    } else {
        // Broadcast not supported. Implement this at the software level as a for loop
        std::vector<tt_cxy_pair> cores_to_write = {};
        for (const auto& chip : all_chip_ids_) {
            if (chips_to_exclude.find(chip) != chips_to_exclude.end()) {
                continue;
            }
            for (const CoreCoord core : get_soc_descriptor(chip).get_all_cores(CoordSystem::VIRTUAL)) {
                if (cols_to_exclude.find(core.x) == cols_to_exclude.end() &&
                    rows_to_exclude.find(core.y) == rows_to_exclude.end()) {
                    write_to_device(mem_ptr, size_in_bytes, chip, core, address, fallback_tlb);
                }
            }
        }
    }
}

void Cluster::broadcast_write_to_cluster(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<chip_id_t>& chips_to_exclude,
    std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& cols_to_exclude,
    const std::string& fallback_tlb) {
    if (arch_name == tt::ARCH::BLACKHOLE) {
        auto architecture_implementation = tt::umd::architecture_implementation::create(arch_name);
        if (cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(9) == cols_to_exclude.end()) {
            log_assert(
                !tensix_or_eth_in_broadcast(cols_to_exclude, architecture_implementation.get()),
                "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Blackhole.");
            if (cols_to_exclude.find(0) == cols_to_exclude.end()) {
                // When broadcast includes column zero do not exclude anything
                std::set<uint32_t> unsafe_rows = {};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = cols_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(9);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude_for_col_0_bcast,
                    cols_to_exclude_for_col_0_bcast,
                    fallback_tlb,
                    false);
            }
            if (cols_to_exclude.find(9) == cols_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_9_bcast = cols_to_exclude;
                cols_to_exclude_for_col_9_bcast.insert(0);
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude,
                    cols_to_exclude_for_col_9_bcast,
                    fallback_tlb,
                    false);
            }
        } else {
            log_assert(
                use_virtual_coords_for_eth_broadcast or
                    valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()),
                "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(
                mem_ptr,
                size_in_bytes,
                address,
                chips_to_exclude,
                rows_to_exclude,
                cols_to_exclude,
                fallback_tlb,
                use_virtual_coords_for_eth_broadcast);
        }
    } else {
        auto architecture_implementation = tt::umd::architecture_implementation::create(arch_name);
        if (cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(5) == cols_to_exclude.end()) {
            log_assert(
                !tensix_or_eth_in_broadcast(cols_to_exclude, architecture_implementation.get()),
                "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Wormhole.");
            if (cols_to_exclude.find(0) == cols_to_exclude.end()) {
                // When broadcast includes column zero Exclude PCIe, ARC and router cores from broadcast explictly,
                // since writing to these is unsafe ERISC FW does not exclude these.
                std::set<uint32_t> unsafe_rows = {2, 3, 4, 8, 9, 10};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = cols_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(5);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude_for_col_0_bcast,
                    cols_to_exclude_for_col_0_bcast,
                    fallback_tlb,
                    false);
            }
            if (cols_to_exclude.find(5) == cols_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_5_bcast = cols_to_exclude;
                cols_to_exclude_for_col_5_bcast.insert(0);
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude,
                    cols_to_exclude_for_col_5_bcast,
                    fallback_tlb,
                    false);
            }
        } else {
            log_assert(
                use_virtual_coords_for_eth_broadcast or
                    valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()),
                "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(
                mem_ptr,
                size_in_bytes,
                address,
                chips_to_exclude,
                rows_to_exclude,
                cols_to_exclude,
                fallback_tlb,
                use_virtual_coords_for_eth_broadcast);
        }
    }
}

int Cluster::remote_arc_msg(
    int chip,
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    constexpr uint64_t ARC_RESET_SCRATCH_ADDR = 0x880030060;
    constexpr uint64_t ARC_RESET_MISC_CNTL_ADDR = 0x880030100;

    auto core = tt_cxy_pair(chip, get_soc_descriptor(chip).get_cores(CoreType::ARC).at(0));

    if ((msg_code & 0xff00) != 0xaa00) {
        log_error("Malformed message. msg_code is 0x{:x} but should be 0xaa..", msg_code);
    }
    log_assert(arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args");  // Only 16 bits are allowed

    uint32_t fw_arg = arg0 | (arg1 << 16);
    int exit_code = 0;

    { write_to_non_mmio_device(&fw_arg, sizeof(fw_arg), core, ARC_RESET_SCRATCH_ADDR + 3 * 4); }

    { write_to_non_mmio_device(&msg_code, sizeof(fw_arg), core, ARC_RESET_SCRATCH_ADDR + 5 * 4); }

    wait_for_non_mmio_flush();
    uint32_t misc = 0;
    read_from_non_mmio_device(&misc, core, ARC_RESET_MISC_CNTL_ADDR, 4);

    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", chip);
        return 1;
    } else {
        misc |= (1 << 16);
        write_to_non_mmio_device(&misc, sizeof(misc), core, ARC_RESET_MISC_CNTL_ADDR);
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed_ms > timeout_ms && timeout_ms != 0) {
                std::stringstream ss;
                ss << std::hex << msg_code;
                throw std::runtime_error(fmt::format(
                    "Timed out after waiting {} ms for device {} ARC to respond to message 0x{}",
                    timeout_ms,
                    chip,
                    ss.str()));
            }

            uint32_t status = 0;
            read_from_non_mmio_device(&status, core, ARC_RESET_SCRATCH_ADDR + 5 * 4, sizeof(status));
            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    read_from_non_mmio_device(return_3, core, ARC_RESET_SCRATCH_ADDR + 3 * 4, sizeof(uint32_t));
                }

                if (return_4 != nullptr) {
                    read_from_non_mmio_device(return_4, core, ARC_RESET_SCRATCH_ADDR + 4 * 4, sizeof(uint32_t));
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == MSG_ERROR_REPLY) {
                log_warning(LogSiliconDriver, "On device {}, message code 0x{:x} not recognized by FW", chip, msg_code);
                exit_code = MSG_ERROR_REPLY;
                break;
            }
        }
    }
    return exit_code;
}

void Cluster::write_to_sysmem(
    const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
    get_local_chip(src_device_id)->write_to_sysmem(channel, mem_ptr, addr, size);
}

void Cluster::read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
    get_local_chip(src_device_id)->read_from_sysmem(channel, mem_ptr, addr, size);
}

void Cluster::set_membar_flag(
    const chip_id_t chip,
    const std::unordered_set<tt_xy_pair>& cores,
    const uint32_t barrier_value,
    const uint32_t barrier_addr,
    const std::string& fallback_tlb) {
    tt_driver_atomics::sfence();  // Ensure that writes before this do not get reordered
    std::unordered_set<tt_xy_pair> cores_synced = {};
    std::vector<uint32_t> barrier_val_vec = {barrier_value};
    for (const auto& core : cores) {
        write_to_device(
            barrier_val_vec.data(),
            barrier_val_vec.size() * sizeof(uint32_t),
            tt_cxy_pair(chip, core),
            barrier_addr,
            fallback_tlb);
    }
    tt_driver_atomics::sfence();  // Ensure that all writes in the Host WC buffer are flushed
    while (cores_synced.size() != cores.size()) {
        for (const auto& core : cores) {
            if (cores_synced.find(core) == cores_synced.end()) {
                uint32_t readback_val;
                read_from_device(
                    &readback_val, tt_cxy_pair(chip, core), barrier_addr, sizeof(std::uint32_t), fallback_tlb);
                if (readback_val == barrier_value) {
                    cores_synced.insert(core);
                } else {
                    log_trace(
                        LogSiliconDriver,
                        "Waiting for core {} to recieve mem bar flag {} in function",
                        core.str(),
                        barrier_value);
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned
    tt_driver_atomics::mfence();
}

void Cluster::insert_host_to_device_barrier(
    const chip_id_t chip,
    const std::unordered_set<tt_xy_pair>& cores,
    const uint32_t barrier_addr,
    const std::string& fallback_tlb) {
    // Ensure that this memory barrier is atomic across processes/threads
    auto lock = get_local_chip(chip)->acquire_mutex(
        MutexType::MEM_BARRIER, get_tt_device(chip)->get_pci_device()->get_device_num());
    set_membar_flag(chip, cores, tt_MemBarFlag::SET, barrier_addr, fallback_tlb);
    set_membar_flag(chip, cores, tt_MemBarFlag::RESET, barrier_addr, fallback_tlb);
}

void Cluster::init_membars() {
    for (const auto& chip : all_chip_ids_) {
        if (cluster_desc->is_chip_mmio_capable(chip)) {
            // TODO: To be removed when this is moved to Chip classes.
            const auto& l1_address_params = get_local_chip(chip)->l1_address_params;
            const auto& dram_address_params = get_local_chip(chip)->dram_address_params;

            set_membar_flag(
                chip,
                workers_per_chip.at(chip),
                tt_MemBarFlag::RESET,
                l1_address_params.tensix_l1_barrier_base,
                "LARGE_WRITE_TLB");
            set_membar_flag(
                chip, eth_cores, tt_MemBarFlag::RESET, l1_address_params.eth_l1_barrier_base, "LARGE_WRITE_TLB");
            set_membar_flag(
                chip, dram_cores, tt_MemBarFlag::RESET, dram_address_params.DRAM_BARRIER_BASE, "LARGE_WRITE_TLB");
        }
    }
}

void Cluster::l1_membar(
    const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt::umd::CoreCoord>& cores) {
    std::unordered_set<tt_xy_pair> cores_xy;
    for (const auto& core : cores) {
        cores_xy.insert(translate_to_api_coords(chip, core));
    }

    if (cluster_desc->is_chip_mmio_capable(chip)) {
        const auto& all_workers = workers_per_chip.at(chip);
        const auto& all_eth = eth_cores;

        // TODO: To be removed when this is moved to Chip classes.
        const auto& l1_address_params = get_local_chip(chip)->l1_address_params;

        if (cores_xy.size()) {
            // Insert barrier on specific cores with L1
            std::unordered_set<tt_xy_pair> workers_to_sync = {};
            std::unordered_set<tt_xy_pair> eth_to_sync = {};

            for (const auto& core : cores_xy) {
                if (all_workers.find(core) != all_workers.end()) {
                    workers_to_sync.insert(core);
                } else if (all_eth.find(core) != all_eth.end()) {
                    eth_to_sync.insert(core);
                } else {
                    log_fatal("Can only insert an L1 Memory barrier on Tensix or Ethernet cores.");
                }
            }
            insert_host_to_device_barrier(
                chip, workers_to_sync, l1_address_params.tensix_l1_barrier_base, fallback_tlb);
            insert_host_to_device_barrier(chip, eth_to_sync, l1_address_params.eth_l1_barrier_base, fallback_tlb);
        } else {
            // Insert barrier on all cores with L1
            insert_host_to_device_barrier(chip, all_workers, l1_address_params.tensix_l1_barrier_base, fallback_tlb);
            insert_host_to_device_barrier(chip, all_eth, l1_address_params.eth_l1_barrier_base, fallback_tlb);
        }
    } else {
        wait_for_non_mmio_flush();
    }
}

void Cluster::dram_membar(
    const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt::umd::CoreCoord>& cores) {
    std::unordered_set<tt_xy_pair> cores_xy;
    for (const auto& core : cores) {
        cores_xy.insert(translate_to_api_coords(chip, core));
    }
    if (cluster_desc->is_chip_mmio_capable(chip)) {
        const auto& dram_address_params = get_local_chip(chip)->dram_address_params;
        if (cores_xy.size()) {
            for (const auto& core : cores_xy) {
                log_assert(
                    dram_cores.find(core) != dram_cores.end(), "Can only insert a DRAM Memory barrier on DRAM cores.");
            }
            insert_host_to_device_barrier(chip, cores_xy, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        } else {
            // Insert Barrier on all DRAM Cores
            insert_host_to_device_barrier(chip, dram_cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
    } else {
        wait_for_non_mmio_flush();
    }
}

void Cluster::dram_membar(
    const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {
    if (cluster_desc->is_chip_mmio_capable(chip)) {
        // TODO: To be removed when this is moved to Chip classes.
        const auto& dram_address_params = get_local_chip(chip)->dram_address_params;

        if (channels.size()) {
            std::unordered_set<tt_xy_pair> dram_cores_to_sync = {};
            for (const auto& chan : channels) {
                dram_cores_to_sync.insert(
                    get_soc_descriptor(chip).get_dram_core_for_channel(chan, 0, CoordSystem::VIRTUAL));
            }
            insert_host_to_device_barrier(
                chip, dram_cores_to_sync, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        } else {
            // Insert Barrier on all DRAM Cores
            insert_host_to_device_barrier(chip, dram_cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
    } else {
        wait_for_non_mmio_flush();
    }
}

void Cluster::write_to_device(
    const void* mem_ptr, uint32_t size, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {
    bool target_is_mmio_capable = cluster_desc->is_chip_mmio_capable(core.chip);
    if (target_is_mmio_capable) {
        if (fallback_tlb == "REG_TLB") {
            write_mmio_device_register(mem_ptr, core, addr, size, fallback_tlb);
        } else {
            write_device_memory(mem_ptr, size, core, addr, fallback_tlb);
        }
    } else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");
        log_assert(
            (get_soc_descriptor(core.chip).get_cores(CoreType::ETH)).size() > 0 && chips_.size() > 1,
            "Cannot issue ethernet writes to a single chip cluster!");
        write_to_non_mmio_device(mem_ptr, size, core, addr);
    }
}

void Cluster::write_to_device(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    chip_id_t chip,
    CoreCoord core,
    uint64_t addr,
    const std::string& tlb_to_use) {
    write_to_device(mem_ptr, size_in_bytes, {(size_t)chip, translate_to_api_coords(chip, core)}, addr, tlb_to_use);
}

void Cluster::read_mmio_device_register(
    void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    get_local_chip(core.chip)->read_from_device_reg(core, mem_ptr, addr, size, fallback_tlb);
}

void Cluster::write_mmio_device_register(
    const void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    get_local_chip(core.chip)->write_to_device_reg(core, mem_ptr, addr, size, fallback_tlb);
}

void Cluster::read_from_device(
    void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    bool target_is_mmio_capable = cluster_desc->is_chip_mmio_capable(core.chip);
    if (target_is_mmio_capable) {
        if (fallback_tlb == "REG_TLB") {
            read_mmio_device_register(mem_ptr, core, addr, size, fallback_tlb);
        } else {
            read_device_memory(mem_ptr, core, addr, size, fallback_tlb);
        }
    } else {
        log_assert(
            arch_name != tt::ARCH::BLACKHOLE,
            "Non-MMIO targets not supported in Blackhole");  // MT: Use only dynamic TLBs and never program static
        log_assert(
            (get_soc_descriptor(core.chip).get_cores(CoreType::TENSIX)).size() > 0 && chips_.size() > 1,
            "Cannot issue ethernet reads from a single chip cluster!");
        read_from_non_mmio_device(mem_ptr, core, addr, size);
    }
}

void Cluster::read_from_device(
    void* mem_ptr, chip_id_t chip, CoreCoord core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    read_from_device(mem_ptr, {(size_t)chip, translate_to_api_coords(chip, core)}, addr, size, fallback_tlb);
}

int Cluster::arc_msg(
    int logical_device_id,
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    if (cluster_desc->is_chip_mmio_capable(logical_device_id)) {
        return pcie_arc_msg(logical_device_id, msg_code, wait_for_done, arg0, arg1, timeout_ms, return_3, return_4);
    } else {
        return remote_arc_msg(logical_device_id, msg_code, wait_for_done, arg0, arg1, timeout_ms, return_3, return_4);
    }
}

void Cluster::send_tensix_risc_reset_to_core(const tt_cxy_pair& core, const TensixSoftResetOptions& soft_resets) {
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type)valid;
    write_to_device(&valid_val, sizeof(uint32_t), core, 0xFFB121B0, "REG_TLB");
    tt_driver_atomics::sfence();
}

void Cluster::send_remote_tensix_risc_reset_to_core(
    const tt_cxy_pair& core, const TensixSoftResetOptions& soft_resets) {
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type)valid;
    write_to_non_mmio_device(&valid_val, sizeof(uint32_t), core, 0xFFB121B0);
    tt_driver_atomics::sfence();
}

int Cluster::set_remote_power_state(const chip_id_t& chip, tt_DevicePowerState device_state) {
    auto mmio_capable_chip_logical = cluster_desc->get_closest_mmio_capable_chip(chip);
    return remote_arc_msg(
        chip, get_power_state_arc_msg(mmio_capable_chip_logical, device_state), true, 0, 0, 1000, NULL, NULL);
}

void Cluster::enable_remote_ethernet_queue(const chip_id_t& chip, int timeout) {
    uint32_t msg_success = 0x0;
    auto timeout_seconds = std::chrono::seconds(timeout);
    auto start = std::chrono::system_clock::now();
    while (msg_success != 1) {
        if (std::chrono::system_clock::now() - start > timeout_seconds) {
            throw std::runtime_error(
                fmt::format("Timed out after waiting {} seconds for DRAM to finish training", timeout));
        }
        int msg_rt = remote_arc_msg(chip, 0xaa58, true, 0xFFFF, 0xFFFF, 1000, &msg_success, NULL);
        if (msg_rt == MSG_ERROR_REPLY) {
            break;
        }
    }
}

void Cluster::broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions& soft_resets) {
    if (chips_.empty()) {
        // Nowhere to broadcast to.
        return;
    }
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type)valid;
    std::set<chip_id_t> chips_to_exclude = {};
    std::set<uint32_t> rows_to_exclude;
    std::set<uint32_t> columns_to_exclude;
    if (arch_name == tt::ARCH::BLACKHOLE) {
        rows_to_exclude = {0, 1};
        columns_to_exclude = {0, 8, 9};
    } else {
        rows_to_exclude = {0, 6};
        columns_to_exclude = {0, 5};
    }
    std::string fallback_tlb = "LARGE_WRITE_TLB";
    broadcast_write_to_cluster(
        &valid_val, sizeof(uint32_t), 0xFFB121B0, chips_to_exclude, rows_to_exclude, columns_to_exclude, fallback_tlb);
    // Ensure that reset signal is globally visible
    wait_for_non_mmio_flush();
}

void Cluster::set_power_state(tt_DevicePowerState device_state) {
    // MT Initial BH - ARC messages not supported in Blackhole
    if (arch_name != tt::ARCH::BLACKHOLE) {
        for (auto& chip : all_chip_ids_) {
            if (cluster_desc->is_chip_mmio_capable(chip)) {
                set_pcie_power_state(device_state);
            } else {
                int exit_code = set_remote_power_state(chip, device_state);
                log_assert(
                    exit_code == 0, "Failed to set power state to {} with exit code: {}", (int)device_state, exit_code);
            }
        }
    } else {
        uint32_t exit_code;
        for (auto& chip : all_chip_ids_) {
            if (device_state == tt_DevicePowerState::BUSY) {
                exit_code = get_tt_device(chip)->get_arc_messenger()->send_message(
                    (uint32_t)tt::umd::blackhole::ArcMessageType::AICLK_GO_BUSY);
            } else {
                exit_code = get_tt_device(chip)->get_arc_messenger()->send_message(
                    (uint32_t)tt::umd::blackhole::ArcMessageType::AICLK_GO_LONG_IDLE);
            }

            if (exit_code != 0) {
                throw std::runtime_error(fmt::format("Setting power state for chip {} failed.", chip));
            }
        }
    }
    wait_for_aiclk_value(device_state);
}

void Cluster::enable_ethernet_queue(int timeout) {
    for (const chip_id_t& chip : all_chip_ids_) {
        auto arch = get_soc_descriptor(chip).arch;

        switch (arch) {
            case tt::ARCH::WORMHOLE_B0: {
                if (cluster_desc->is_chip_mmio_capable(chip)) {
                    enable_local_ethernet_queue(chip, timeout);
                } else {
                    enable_remote_ethernet_queue(chip, timeout);
                }

                break;
                case tt::ARCH::BLACKHOLE:
                    log_assert(false, "Arch BLACKHOLE doesn't support ethernet queues yet");
            }
            default: {
                break;
            }
        }
    }
}

void Cluster::deassert_resets_and_set_power_state() {
    // Assert tensix resets on all chips in cluster
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);

    // MT Initial BH - ARC messages not supported in Blackhole
    if (arch_name != tt::ARCH::BLACKHOLE) {
        // Send ARC Messages to deassert RISCV resets
        for (auto& chip_id : local_chip_ids_) {
            arc_msg(
                chip_id,
                0xaa00 |
                    get_tt_device(chip_id)->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(),
                true,
                0,
                0);
        }
        if (cluster_desc != nullptr) {
            for (const chip_id_t& chip : all_chip_ids_) {
                if (!cluster_desc->is_chip_mmio_capable(chip)) {
                    auto mmio_capable_chip_logical = cluster_desc->get_closest_mmio_capable_chip(chip);
                    auto tt_device = get_tt_device(mmio_capable_chip_logical);
                    remote_arc_msg(
                        chip,
                        0xaa00 | tt_device->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(),
                        true,
                        0x0,
                        0x0,
                        1000,
                        NULL,
                        NULL);
                }
            }
            enable_ethernet_queue(30);
        }
    }

    // Set power state to busy
    set_power_state(tt_DevicePowerState::BUSY);
}

void Cluster::verify_eth_fw() {
    for (const auto& chip : all_chip_ids_) {
        uint32_t fw_version;
        std::vector<uint32_t> fw_versions;
        for (const CoreCoord eth_core : get_soc_descriptor(chip).get_cores(CoreType::ETH)) {
            read_from_device(
                &fw_version,
                chip,
                eth_core,
                get_chip(chip)->l1_address_params.fw_version_addr,
                sizeof(uint32_t),
                "LARGE_READ_TLB");
            fw_versions.push_back(fw_version);
        }
        verify_sw_fw_versions(chip, SW_VERSION, fw_versions);
        eth_fw_version = tt_version(fw_versions.at(0));
    }
}

void Cluster::verify_sw_fw_versions(int device_id, std::uint32_t sw_version, std::vector<std::uint32_t>& fw_versions) {
    tt_version sw(sw_version), fw_first_eth_core(fw_versions.at(0));
    log_info(
        LogSiliconDriver,
        "Software version {}, Ethernet FW version {} (Device {})",
        sw.str(),
        fw_first_eth_core.str(),
        device_id);
    for (std::uint32_t& fw_version : fw_versions) {
        tt_version fw(fw_version);
        log_assert(fw == fw_first_eth_core, "FW versions are not the same across different ethernet cores");
        log_assert(sw.major == fw.major, "SW/FW major version number out of sync");
        log_assert(sw.minor <= fw.minor, "SW version is newer than FW version");
    }

    // Min ERISC FW version required to support ordered writes is 6.4.0
    use_ethernet_ordered_writes &= fw_first_eth_core >= tt_version(6, 4, 0);
    // Min ERISC FW version required to support ethernet broadcast is 6.5.0.
    use_ethernet_broadcast &= fw_first_eth_core >= tt_version(6, 5, 0);
    // Virtual coordinates can be used for broadcast headers if ERISC FW >= 6.8.0 and NOC translation is enabled
    // Temporarily enable this feature for 6.7.241 as well for testing.
    use_virtual_coords_for_eth_broadcast &=
        (fw_first_eth_core >= tt_version(6, 8, 0) || fw_first_eth_core == tt_version(6, 7, 241)) &&
        get_soc_descriptor(device_id).noc_translation_enabled;
}

void Cluster::start_device(const tt_device_params& device_params) {
    if (device_params.init_device) {
        initialize_pcie_devices();
        // MT Initial BH - Ethernet firmware not present in Blackhole
        if (arch_name == tt::ARCH::WORMHOLE_B0) {
            verify_eth_fw();
        }
        deassert_resets_and_set_power_state();
    }
}

void Cluster::close_device() {
    set_power_state(tt_DevicePowerState::LONG_IDLE);
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);
}

std::uint32_t Cluster::get_num_dram_channels(std::uint32_t device_id) {
    log_assert(
        all_chip_ids_.find(device_id) != all_chip_ids_.end(),
        "Querying DRAM parameters for a device that does not exist.");
    return get_soc_descriptor(device_id).get_num_dram_channels();
}

std::uint64_t Cluster::get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    log_assert(channel < get_num_dram_channels(device_id), "Querying size for a device channel that does not exist.");
    return get_soc_descriptor(device_id).dram_bank_size;  // Space per channel is identical for now
}

std::uint32_t Cluster::get_num_host_channels(std::uint32_t device_id) {
    auto devices = get_target_mmio_device_ids();
    log_assert(
        devices.find(device_id) != devices.end(),
        "Querying Host Address parameters for a non-mmio device or a device does not exist.");
    return get_local_chip(device_id)->get_sysmem_manager()->get_num_host_mem_channels();
}

std::uint32_t Cluster::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    log_assert(channel < get_num_host_channels(device_id), "Querying size for a host channel that does not exist.");
    hugepage_mapping hugepage_map = get_local_chip(device_id)->get_sysmem_manager()->get_hugepage_mapping(channel);
    log_assert(hugepage_map.mapping_size, "Host channel size can only be queried after the device has been started.");
    return hugepage_map.mapping_size;
}

std::uint32_t Cluster::get_numa_node_for_pcie_device(std::uint32_t device_id) {
    return get_tt_device(device_id)->get_pci_device()->get_numa_node();
}

std::uint64_t Cluster::get_pcie_base_addr_from_device(const chip_id_t chip_id) const {
    // TODO: Should probably be lowered to TTDevice.
    tt::ARCH arch = get_soc_descriptor(chip_id).arch;
    if (arch == tt::ARCH::WORMHOLE_B0) {
        return 0x800000000;
    } else if (arch == tt::ARCH::BLACKHOLE) {
        // Enable 4th ATU window.
        return 1ULL << 60;
    } else {
        return 0;
    }
}

tt_version Cluster::get_ethernet_fw_version() const {
    log_assert(arch_name == tt::ARCH::WORMHOLE_B0, "Can only get Ethernet FW version for Wormhole architectures.");
    log_assert(
        eth_fw_version.major != 0xffff and eth_fw_version.minor != 0xff and eth_fw_version.patch != 0xff,
        "Device must be started before querying Ethernet FW version.");
    return eth_fw_version;
}

void Cluster::set_barrier_address_params(const barrier_address_params& barrier_address_params_) {
    for (auto chip_id : local_chip_ids_) {
        get_local_chip(chip_id)->set_barrier_address_params(barrier_address_params_);
    }
}

// TODO: This is a temporary function while we're switching between the old and the new API.
// Eventually, this function should be so small it would be obvioud to remove.
tt_xy_pair Cluster::translate_to_api_coords(const chip_id_t chip, const tt::umd::CoreCoord core_coord) const {
    return get_soc_descriptor(chip).translate_coord_to(core_coord, CoordSystem::VIRTUAL);
}

tt_xy_pair Cluster::translate_chip_coord_virtual_to_translated(const chip_id_t chip_id, const tt_xy_pair core) const {
    CoreCoord core_coord = get_soc_descriptor(chip_id).get_coord_at(core, CoordSystem::VIRTUAL);
    // Since NOC1 and translated coordinate space overlaps for Tensix cores on Blackhole,
    // Tensix cores are always used in translated space. Other cores are used either in
    // NOC1 or translated space depending on the umd_use_noc1 flag.
    // On Wormhole Tensix can use NOC1 space if umd_use_noc1 is set to true.
    if (get_soc_descriptor(chip_id).noc_translation_enabled) {
        if (get_soc_descriptor(chip_id).arch == tt::ARCH::BLACKHOLE) {
            if (core_coord.core_type == CoreType::TENSIX || !umd_use_noc1) {
                return get_soc_descriptor(chip_id).translate_coord_to(core_coord, CoordSystem::TRANSLATED);
            } else {
                return get_soc_descriptor(chip_id).translate_coord_to(core_coord, CoordSystem::NOC1);
            }
        } else {
            return get_soc_descriptor(chip_id).translate_coord_to(
                core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
        }
    } else {
        return get_soc_descriptor(chip_id).translate_coord_to(
            core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
    }
}

std::unique_ptr<tt_ClusterDescriptor> Cluster::create_cluster_descriptor(std::string sdesc_path) {
    std::map<int, PciDeviceInfo> pci_device_info = PCIDevice::enumerate_devices_info();
    if (pci_device_info.begin()->second.get_arch() == tt::ARCH::BLACKHOLE) {
        std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

        std::unordered_map<chip_id_t, std::unique_ptr<Chip>> chips;
        chip_id_t chip_id = 0;
        for (auto& device_id : pci_device_ids) {
            std::unique_ptr<LocalChip> chip = nullptr;
            if (sdesc_path.empty()) {
                chip = std::make_unique<LocalChip>(TTDevice::create(device_id));
            } else {
                chip = std::make_unique<LocalChip>(sdesc_path, TTDevice::create(device_id));
            }
            chips.emplace(chip_id, std::move(chip));
            chip_id++;
        }

        return Cluster::create_cluster_descriptor(chips);
    } else {
        std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

        std::vector<std::unique_ptr<TTDevice>> tt_devices;
        for (auto& device_id : pci_device_ids) {
            std::unique_ptr<TTDevice> tt_device = TTDevice::create(device_id);
            tt_devices.push_back(std::move(tt_device));
        }

        // Topology discovery from source is supported for Wormhole UBB at the moment,
        // other Wormhole specs need to go through a legacy create-ethernet-map.
        if (!tt_devices.empty() && tt_devices[0]->get_board_type() != BoardType::UBB) {
            LockManager lock_manager;
            lock_manager.initialize_mutex(MutexType::CREATE_ETH_MAP, false);
            std::unique_ptr<tt_ClusterDescriptor> cluster_desc = nullptr;
            {
                auto lock = lock_manager.acquire_mutex(MutexType::CREATE_ETH_MAP);
                cluster_desc = tt_ClusterDescriptor::create();
            }
            lock_manager.clear_mutex(MutexType::CREATE_ETH_MAP);
            return cluster_desc;
        }

        std::unordered_map<chip_id_t, std::unique_ptr<Chip>> chips;
        chip_id_t chip_id = 0;
        for (auto& device_id : pci_device_ids) {
            std::unique_ptr<LocalChip> chip = nullptr;
            if (sdesc_path.empty()) {
                chip = std::make_unique<LocalChip>(TTDevice::create(device_id));
            } else {
                chip = std::make_unique<LocalChip>(sdesc_path, TTDevice::create(device_id));
            }
            chips.emplace(chip_id, std::move(chip));
            chip_id++;
        }

        return Cluster::create_cluster_descriptor(chips);
    }
}

std::unique_ptr<tt_ClusterDescriptor> Cluster::create_cluster_descriptor(
    const std::unordered_map<chip_id_t, std::unique_ptr<tt::umd::Chip>>& chips) {
    std::unique_ptr<tt_ClusterDescriptor> desc = std::unique_ptr<tt_ClusterDescriptor>(new tt_ClusterDescriptor());

    if (chips.empty()) {
        return desc;
    }

    for (auto& it : chips) {
        const chip_id_t chip_id = it.first;
        const std::unique_ptr<Chip>& chip = it.second;

        // TODO: Use the line below when we can read asic location from the Blackhole telemetry.
        // Until then we have to read it from ETH core.
        // desc->add_chip_uid(chip_id, chip->get_chip_info().chip_uid);

        // TODO: Remove this when we can read asic location from the Blackhole telemetry.
        // Until then we have to read it from ETH core.
        const std::vector<CoreCoord> eth_cores = chip->get_soc_descriptor().get_cores(CoreType::ETH);
        for (size_t eth_channel = 0; eth_channel < eth_cores.size(); eth_channel++) {
            const CoreCoord& eth_core = eth_cores[eth_channel];
            TTDevice* tt_device = chip->get_tt_device();
            blackhole::boot_results_t boot_results;

            tt_device->read_from_device(
                (uint8_t*)&boot_results,
                tt_xy_pair(eth_core.x, eth_core.y),
                blackhole::BOOT_RESULTS_ADDR,
                sizeof(boot_results));

            // We can read the asic location only from active ETH cores.
            if (boot_results.eth_status.port_status == blackhole::port_status_e::PORT_UP) {
                const blackhole::chip_info_t& local_info = boot_results.local_info;
                desc->add_chip_uid(chip_id, ChipUID{chip->get_chip_info().chip_uid.board_id, local_info.asic_location});
            }
        }
    }

    for (auto& it : chips) {
        const chip_id_t chip_id = it.first;
        const std::unique_ptr<Chip>& chip = it.second;

        desc->all_chips.insert(chip_id);
        desc->chip_arch.insert({chip_id, chip->get_tt_device()->get_arch()});

        desc->chips_with_mmio.insert({chip_id, chip->get_tt_device()->get_pci_device()->get_device_num()});

        desc->chip_board_type.insert({chip_id, chip->get_chip_info().board_type});

        desc->noc_translation_enabled.insert({chip_id, chip->get_chip_info().noc_translation_enabled});
        desc->harvesting_masks.insert({chip_id, chip->get_chip_info().harvesting_masks.tensix_harvesting_mask});
        desc->dram_harvesting_masks.insert({chip_id, chip->get_chip_info().harvesting_masks.dram_harvesting_mask});
        desc->eth_harvesting_masks.insert({chip_id, chip->get_chip_info().harvesting_masks.eth_harvesting_mask});
    }

    if (chips.begin()->second->get_tt_device()->get_arch() == tt::ARCH::BLACKHOLE) {
        for (auto& it : chips) {
            const chip_id_t chip_id = it.first;
            const std::unique_ptr<Chip>& chip = it.second;

            const std::vector<CoreCoord> eth_cores = chip->get_soc_descriptor().get_cores(CoreType::ETH);

            for (size_t eth_channel = 0; eth_channel < eth_cores.size(); eth_channel++) {
                const CoreCoord& eth_core = eth_cores[eth_channel];
                TTDevice* tt_device = chip->get_tt_device();
                blackhole::boot_results_t boot_results;

                tt_device->read_from_device(
                    (uint8_t*)&boot_results,
                    tt_xy_pair(eth_core.x, eth_core.y),
                    blackhole::BOOT_RESULTS_ADDR,
                    sizeof(boot_results));

                if (boot_results.eth_status.port_status == blackhole::port_status_e::PORT_UP) {
                    // active eth core
                    desc->active_eth_channels[chip_id].insert(eth_channel);
                    log_debug(
                        LogSiliconDriver, "Eth core ({}, {}) on chip {} is active", eth_core.x, eth_core.y, chip_id);
                    const blackhole::chip_info_t& local_info = boot_results.local_info;
                    const blackhole::chip_info_t& remote_info = boot_results.remote_info;

                    chip_id_t local_chip_id = desc->get_chip_id(local_info.get_chip_uid()).value();
                    std::optional<chip_id_t> remote_chip_id = desc->get_chip_id(remote_info.get_chip_uid());
                    if (!remote_chip_id.has_value()) {
                        log_debug(
                            LogSiliconDriver,
                            "Eth core ({}, {}) on chip {} is connected to an chip with board_id {} not present in the "
                            "target devices opened by this driver.",
                            eth_core.x,
                            eth_core.y,
                            chip_id,
                            remote_info.get_chip_uid().board_id);
                    } else {
                        const CoreCoord logical_remote_coord = chips.at(remote_chip_id.value())
                                                                   ->get_soc_descriptor()
                                                                   .translate_coord_to(
                                                                       blackhole::ETH_CORES_NOC0[remote_info.eth_id],
                                                                       CoordSystem::PHYSICAL,
                                                                       CoordSystem::LOGICAL);
                        // Adding a connection only one way, the other chip should add it another way.
                        desc->ethernet_connections[local_chip_id][eth_channel] = {
                            remote_chip_id.value(), logical_remote_coord.y};
                    }
                } else if (boot_results.eth_status.port_status == blackhole::port_status_e::PORT_DOWN) {
                    // active eth core, just with link being down.
                    desc->active_eth_channels[chip_id].insert(eth_channel);
                    log_debug(
                        LogSiliconDriver,
                        "Port on eth core ({}, {}) on chip {} is down",
                        eth_core.x,
                        eth_core.y,
                        chip_id);
                } else if (boot_results.eth_status.port_status == blackhole::port_status_e::PORT_UNUSED) {
                    // idle core
                    desc->idle_eth_channels[chip_id].insert(eth_channel);
                    log_debug(LogSiliconDriver, "Eth core ({}, {}) on chip {} is idle");
                } else if (boot_results.eth_status.port_status == blackhole::port_status_e::PORT_UNKNOWN) {
                    log_debug(
                        LogSiliconDriver,
                        "Port on eth core ({}, {}) on chip {} is in unknown state",
                        eth_core.x,
                        eth_core.y,
                        chip_id);
                }
            }
        }
    } else {
        ubb_eth_connections(chips, desc);
    }

    desc->enable_all_devices();

    desc->fill_chips_grouped_by_closest_mmio();

    return desc;
}

std::string Cluster::serialize() { return Cluster::create_cluster_descriptor()->serialize(); }

std::filesystem::path Cluster::serialize_to_file(const std::filesystem::path& dest_file) {
    std::filesystem::path file_path = dest_file;
    if (file_path.empty()) {
        file_path = tt_ClusterDescriptor::get_default_cluster_descriptor_file_path();
    }
    Cluster::create_cluster_descriptor()->serialize_to_file(file_path);
    return file_path;
}

}  // namespace tt::umd
