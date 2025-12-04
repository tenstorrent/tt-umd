// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/cluster.hpp"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fmt/format.h>
#include <fmt/ranges.h>  // Needed to format vectors
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <yaml-cpp/yaml.h>

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
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "api/umd/device/cluster.hpp"
#include "api/umd/device/types/core_coordinates.hpp"
#include "assert.hpp"
#include "hugepage.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/chip/mock_chip.hpp"
#include "umd/device/chip/remote_chip.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/topology/topology_discovery_blackhole.hpp"
#include "umd/device/topology/topology_discovery_wormhole.hpp"
#include "umd/device/topology/topology_utils.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/semver.hpp"
#include "utils.hpp"

extern bool umd_use_noc1;

static constexpr uint32_t REMOTE_CMD_NOC_BIT = 9;

// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------

#include <fstream>
#include <iomanip>
#include <thread>

#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

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

const SocDescriptor& Cluster::get_soc_descriptor(ChipId chip_id) const {
    return get_chip(chip_id)->get_soc_descriptor();
}

void Cluster::verify_sysmem_initialized() {
    for (const ChipId& chip_id : local_chip_ids_) {
        bool hugepages_initialized =
            (get_chip(chip_id)->get_sysmem_manager()->get_hugepage_mapping(0).mapping != nullptr);
        // Large writes to remote chips require hugepages to be initialized.
        // Conservative assert - end workload if remote chips present but hugepages not initialized (failures caused
        // if using remote only for small transactions)
        if (remote_chip_ids_.size()) {
            TT_ASSERT(
                hugepages_initialized, "Hugepages must be successfully initialized if workload contains remote chips!");
        }
        if (!hugepages_initialized) {
            log_warning(LogUMD, "No hugepage mapping at device {}.", chip_id);
        }
    }
}

void Cluster::log_device_summary() {
    switch (cluster_desc->get_io_device_type()) {
        case IODeviceType::PCIe:
            log_pci_device_summary();
            break;
        case IODeviceType::JTAG:
            // Currently no specific device logging needed for JTAG.
            break;
        default:
            TT_THROW("Unknown device type for logging.");
            break;
    }
}

void Cluster::log_pci_device_summary() {
    if (local_chip_ids_.empty()) {
        return;
    }

    auto first_pci_device = chips_.at(*local_chip_ids_.begin())->get_tt_device()->get_pci_device();
    if (!first_pci_device) {
        return;
    }

    bool expected_iommu_state = first_pci_device->is_iommu_enabled();
    std::string kmd_version = PCIDevice::read_kmd_version().to_string();

    // Check IOMMU status consistency across all devices.
    bool all_devices_same_iommu_state = true;
    auto iommu_state_str = [](bool enabled) { return enabled ? "enabled" : "disabled"; };

    for (ChipId chip_id : local_chip_ids_) {
        auto pci_device = chips_.at(chip_id)->get_tt_device()->get_pci_device();
        if (!pci_device) {
            continue;
        }
        bool current_iommu_state = pci_device->is_iommu_enabled();
        if (current_iommu_state != expected_iommu_state) {
            log_warning(
                LogUMD,
                "IOMMU state mismatch for chip {}: expected {}, got {}",
                chip_id,
                iommu_state_str(expected_iommu_state),
                iommu_state_str(current_iommu_state));
            all_devices_same_iommu_state = false;
        }

        if (!all_devices_same_iommu_state) {
            break;
        }
    }

    if (all_devices_same_iommu_state) {
        log_info(LogUMD, "IOMMU: {}", iommu_state_str(expected_iommu_state));
    }

    log_info(LogUMD, "KMD version: {}", kmd_version);
}

void Cluster::construct_cluster(const uint32_t& num_host_mem_ch_per_mmio_device, const ChipType& chip_type) {
    // TODO: work on removing this member altogether. Currently assumes all have the same arch.
    arch_name = chips_.empty() ? tt::ARCH::Invalid : chips_.begin()->second->get_soc_descriptor().arch;

    eth_fw_version = cluster_desc->eth_fw_version;

    if (chip_type == ChipType::SILICON) {
        std::vector<int> pci_ids;
        auto mmio_id_map = cluster_desc->get_chips_with_mmio();
        for (ChipId local_chip_id : local_chip_ids_) {
            pci_ids.push_back(mmio_id_map.at(local_chip_id));
        }
        log_info(
            LogUMD,
            "Opening local chip ids/{} ids: {}/{} and remote chip ids {}",
            DeviceTypeToString.at(cluster_desc->get_io_device_type()),
            local_chip_ids_,
            pci_ids,
            remote_chip_ids_);
        log_device_summary();

        if (arch_name == tt::ARCH::WORMHOLE_B0) {
            // Min ERISC FW version required to support ethernet broadcast is 6.5.0.
            use_ethernet_broadcast &= eth_fw_version >= erisc_firmware::WH_MIN_ERISC_FW_ETH_BROADCAST_SUPPORTED;
            // Virtual coordinates can be used for broadcast headers if ERISC FW >= 6.8.0 and NOC translation is enabled
            // Temporarily enable this feature for 6.7.241 as well for testing.
            use_translated_coords_for_eth_broadcast = true;
            for (const auto& chip : all_chip_ids_) {
                use_translated_coords_for_eth_broadcast &=
                    (eth_fw_version >= erisc_firmware::WH_MIN_ERISC_FW_ETH_BROADCAST_VIRTUAL_COORDS ||
                     eth_fw_version == semver_t(6, 7, 241)) &&
                    get_soc_descriptor(chip).noc_translation_enabled;
            }
        }

        if (cluster_desc->get_io_device_type() == IODeviceType::PCIe) {
            verify_sysmem_initialized();
        }
    }

    // Disable dependency to ethernet firmware for all BH devices and WH devices with all chips having MMIO (e.g. UBB
    // Galaxy, or P300).
    if (remote_chip_ids_.empty() || chip_type != ChipType::SILICON || arch_name == tt::ARCH::BLACKHOLE) {
        use_ethernet_broadcast = false;
    }
}

std::unique_ptr<Chip> Cluster::construct_chip_from_cluster(
    ChipId chip_id,
    const ChipType& chip_type,
    ClusterDescriptor* cluster_desc,
    SocDescriptor& soc_desc,
    int num_host_mem_channels,
    const std::filesystem::path& simulator_directory) {
    if (chip_type == ChipType::MOCK) {
        return std::make_unique<MockChip>(soc_desc);
    }
    if (chip_type == ChipType::SIMULATION) {
#ifdef TT_UMD_BUILD_SIMULATION
        log_info(LogUMD, "Creating Simulation device");
        return SimulationChip::create(simulator_directory, soc_desc, chip_id, cluster_desc->get_number_of_chips());
#else
        throw std::runtime_error(
            "Simulation device is not supported in this build. Set '-DTT_UMD_BUILD_SIMULATION=ON' during cmake "
            "configuration to enable simulation device.");
#endif
    }

    if (cluster_desc->is_chip_mmio_capable(chip_id)) {
        auto chip = LocalChip::create(
            (cluster_desc->get_chips_with_mmio().at(chip_id)),
            soc_desc,
            num_host_mem_channels,
            cluster_desc->io_device_type);

        if (cluster_desc->get_arch(chip_id) == tt::ARCH::WORMHOLE_B0) {
            // Remote transfer currently supported only for wormhole.
            chip->set_remote_transfer_ethernet_cores(cluster_desc->get_active_eth_channels(chip_id));
        }
        return chip;
    } else {
        ChipId gateway_id = cluster_desc->get_closest_mmio_capable_chip(chip_id);
        LocalChip* local_chip = get_local_chip(gateway_id);
        const auto& active_channels = cluster_desc->get_active_eth_channels(gateway_id);
        return RemoteChip::create(
            local_chip,
            cluster_desc->get_chip_location(chip_id),
            cluster_desc->get_active_eth_channels(gateway_id),
            soc_desc);
    }
}

SocDescriptor Cluster::construct_soc_descriptor(
    const std::string& soc_desc_path,
    ChipId chip_id,
    ChipType chip_type,
    ClusterDescriptor* cluster_desc,
    bool perform_harvesting,
    HarvestingMasks& simulated_harvesting_masks) {
    bool chip_in_cluster_descriptor =
        cluster_desc->get_all_chips().find(chip_id) != cluster_desc->get_all_chips().end();

    // In case of SILICON chip type, this chip has to exist in the cluster descriptor. But it doesn't have to exist in
    // case of Mock or Simulation chip type.
    if (chip_type == ChipType::SILICON && !chip_in_cluster_descriptor) {
        throw std::runtime_error(
            fmt::format("Chip {} not found in cluster descriptor. Cannot create device.", chip_id));
    }

    ChipInfo chip_info;
    if (chip_in_cluster_descriptor) {
        chip_info.noc_translation_enabled = cluster_desc->get_noc_translation_table_en().at(chip_id);
        chip_info.harvesting_masks =
            get_harvesting_masks(chip_id, cluster_desc, perform_harvesting, simulated_harvesting_masks);
        chip_info.board_type = cluster_desc->get_board_type(chip_id);
        chip_info.asic_location = cluster_desc->get_asic_location(chip_id);
    }

    if (soc_desc_path.empty()) {
        tt::ARCH arch = chip_in_cluster_descriptor ? cluster_desc->get_arch(chip_id) : tt::ARCH::WORMHOLE_B0;

        return SocDescriptor(arch, chip_info);

    } else {
        SocDescriptor soc_desc = SocDescriptor(soc_desc_path, chip_info);

        // In this case, check that the passed soc descriptor architecture doesn't conflate with the one in the cluster
        // descriptor.
        if (chip_in_cluster_descriptor && soc_desc.arch != cluster_desc->get_arch(chip_id)) {
            throw std::runtime_error(fmt::format(
                "Passed soc descriptor has {} arch, but for chip id {} has arch {}",
                arch_to_str(soc_desc.arch),
                chip_id,
                arch_to_str(cluster_desc->get_arch(chip_id))));
        }

        return soc_desc;
    }
}

void Cluster::add_chip(const ChipId& chip_id, const ChipType& chip_type, std::unique_ptr<Chip> chip) {
    TT_ASSERT(
        chips_.find(chip_id) == chips_.end(),
        "Chip with id {} already exists in cluster. Cannot add another chip with the same id.",
        chip_id);
    all_chip_ids_.insert(chip_id);
    // All non silicon chip types are considered local chips.
    if (chip_type == ChipType::SIMULATION || cluster_desc->is_chip_mmio_capable(chip_id)) {
        local_chip_ids_.insert(chip_id);
    } else {
        remote_chip_ids_.insert(chip_id);
    }
    chips_.emplace(chip_id, std::move(chip));
}

HarvestingMasks Cluster::get_harvesting_masks(
    ChipId chip_id,
    ClusterDescriptor* cluster_desc,
    bool perform_harvesting,
    HarvestingMasks& simulated_harvesting_masks) {
    if (!perform_harvesting) {
        log_info(LogUMD, "Skipping harvesting for chip {}.", chip_id);
        return HarvestingMasks{};
    }

    HarvestingMasks cluster_harvesting_masks = cluster_desc->get_harvesting_masks(chip_id);
    log_info(
        LogUMD,
        "Harvesting masks for chip {} tensix: {:#x} dram: {:#x} eth: {:#x} pcie: {:#x} l2cpu: {:#x}",
        chip_id,
        cluster_harvesting_masks.tensix_harvesting_mask | simulated_harvesting_masks.tensix_harvesting_mask,
        cluster_harvesting_masks.dram_harvesting_mask | simulated_harvesting_masks.dram_harvesting_mask,
        cluster_harvesting_masks.eth_harvesting_mask | simulated_harvesting_masks.eth_harvesting_mask,
        cluster_harvesting_masks.pcie_harvesting_mask | simulated_harvesting_masks.pcie_harvesting_mask,
        cluster_harvesting_masks.l2cpu_harvesting_mask | simulated_harvesting_masks.l2cpu_harvesting_mask);

    return cluster_harvesting_masks | simulated_harvesting_masks;
}

Cluster::Cluster(ClusterOptions options) {
    // If the cluster descriptor is not provided, create a new one.
    ClusterDescriptor* temp_full_cluster_desc = options.cluster_descriptor;
    std::unique_ptr<ClusterDescriptor> temp_full_cluster_desc_ptr;

    bool is_ttsim_simulation =
        (options.chip_type == ChipType::SIMULATION && options.simulator_directory.extension() == ".so");

    // We need to constuct a cluster descriptor if a custom one was not passed.
    if (temp_full_cluster_desc == nullptr) {
        if (options.chip_type == ChipType::SILICON) {
            // If no custom descriptor is provided, we need to create a new one from the existing devices on the system.
            temp_full_cluster_desc_ptr = Cluster::create_cluster_descriptor(options.sdesc_path, options.io_device_type);
        } else {
            // If no custom descriptor is provided, in case of mock or simulation chip type, we create a mock cluster
            // descriptor from passed target devices.
            auto arch = tt::ARCH::WORMHOLE_B0;
#ifdef TT_UMD_BUILD_SIMULATION
            if (options.chip_type == ChipType::SIMULATION) {
                if (options.sdesc_path.empty()) {
                    options.sdesc_path =
                        SimulationChip::get_soc_descriptor_path_from_simulator_path(options.simulator_directory);
                }
                arch = SocDescriptor::get_arch_from_soc_descriptor_path(options.sdesc_path);
            }
#endif
            // Noc translation is enabled for mock chips and for ttsim simulation, but disabled for versim/vcs
            // simulation.
            bool noc_translation_enabled = options.chip_type == ChipType::MOCK || is_ttsim_simulation;
            temp_full_cluster_desc_ptr =
                ClusterDescriptor::create_mock_cluster(options.target_devices, arch, noc_translation_enabled);
        }
        temp_full_cluster_desc = temp_full_cluster_desc_ptr.get();
    }

    // If target devices were passed, we want to honour it by constraining the cluster descriptor to only include the
    // chips in the target devices. Note that we can skip this step in case of mock cluster descriptor, since it was
    // already created using the target devices.
    if (!options.target_devices.empty() && options.chip_type == ChipType::SILICON) {
        // If target devices are passed create constrained cluster descriptor which only contains the chips to be in
        // this Cluster.
        cluster_desc =
            ClusterDescriptor::create_constrained_cluster_descriptor(temp_full_cluster_desc, options.target_devices);
#ifdef TT_UMD_BUILD_SIMULATION
    } else if (options.chip_type == ChipType::SIMULATION && options.cluster_descriptor) {
        // Filter devices only when a cluster descriptor is passed for simulation.
        // Note that this is filtered based on logical chip ids, which is different from how silicon chips are filtered.
        auto visible_devices = utils::get_visible_devices(options.target_devices);
        if (!visible_devices.empty()) {
            cluster_desc =
                ClusterDescriptor::create_constrained_cluster_descriptor(temp_full_cluster_desc, visible_devices);
        } else {
            cluster_desc = std::make_unique<ClusterDescriptor>(*temp_full_cluster_desc);
        }
#endif
    } else {
        // If no target devices are passed, we can use the full cluster.
        // Note that the pointer is being dereferenced below, that means that the default copy constructor will be
        // called for ClusterDescriptor to construct the object which will end up in the unique_ptr, note that the
        // line below doesn't take ownership of already existing object pointed to by temp_full_cluster_desc.
        cluster_desc = std::make_unique<ClusterDescriptor>(*temp_full_cluster_desc);
    }

    // Construct all the required chips from the cluster descriptor.
    for (auto& chip_id : cluster_desc->get_chips_local_first(cluster_desc->get_all_chips())) {
        // Combine passed simulated_harvesting_masks.
        HarvestingMasks simulated_harvesting_masks =
            options.simulated_harvesting_masks | ((options.simulated_harvesting_masks_per_chip.find(chip_id) !=
                                                   options.simulated_harvesting_masks_per_chip.end())
                                                      ? options.simulated_harvesting_masks_per_chip.at(chip_id)
                                                      : HarvestingMasks{});
        SocDescriptor soc_desc = construct_soc_descriptor(
            options.sdesc_path,
            chip_id,
            options.chip_type,
            cluster_desc.get(),
            options.perform_harvesting,
            simulated_harvesting_masks);

        add_chip(
            chip_id,
            options.chip_type,
            construct_chip_from_cluster(
                chip_id,
                options.chip_type,
                cluster_desc.get(),
                soc_desc,
                options.num_host_mem_ch_per_mmio_device,
                options.simulator_directory));
    }

    construct_cluster(options.num_host_mem_ch_per_mmio_device, options.chip_type);
}

void Cluster::configure_active_ethernet_cores_for_mmio_device(
    ChipId mmio_chip, const std::unordered_set<CoreCoord>& active_eth_cores_per_chip) {
    // The ethernet cores that should be used for remote transfer are set in the RemoteCommunication structure.
    // This structure is used by remote chips. So we need to find all remote chips that use the passed in mmio_chip,
    // and set the active ethernet cores for them.
    for (const auto& remote_chip_id : remote_chip_ids_) {
        if (cluster_desc->get_closest_mmio_capable_chip(remote_chip_id) == mmio_chip) {
            get_remote_chip(remote_chip_id)->set_remote_transfer_ethernet_cores(active_eth_cores_per_chip);
        }
    }
    // Local chips hold communication primitives for broadcasting, so we have to set this up for them as well.
    get_local_chip(mmio_chip)->set_remote_transfer_ethernet_cores(active_eth_cores_per_chip);
}

std::set<ChipId> Cluster::get_target_device_ids() { return all_chip_ids_; }

std::set<ChipId> Cluster::get_target_mmio_device_ids() { return local_chip_ids_; }

std::set<ChipId> Cluster::get_target_remote_device_ids() { return remote_chip_ids_; }

void Cluster::assert_risc_reset() { broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET); }

void Cluster::deassert_risc_reset() { broadcast_tensix_risc_reset_to_cluster(TENSIX_DEASSERT_SOFT_RESET); }

void Cluster::deassert_risc_reset_at_core(
    const ChipId chip, const CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    get_chip(chip)->send_tensix_risc_reset(core, soft_resets);
}

void Cluster::assert_risc_reset_at_core(
    const ChipId chip, const CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    get_chip(chip)->send_tensix_risc_reset(core, soft_resets);
}

RiscType Cluster::get_risc_reset_state(const ChipId chip, const CoreCoord core) {
    return get_chip(chip)->get_risc_reset_state(core);
}

void Cluster::assert_risc_reset(const ChipId chip, const CoreCoord core, const RiscType risc_type) {
    get_chip(chip)->assert_risc_reset(core, risc_type);
}

void Cluster::deassert_risc_reset(
    const ChipId chip, const CoreCoord core, const RiscType risc_type, bool staggered_start) {
    get_chip(chip)->deassert_risc_reset(core, risc_type, staggered_start);
}

ClusterDescriptor* Cluster::get_cluster_description() { return cluster_desc.get(); }

Writer Cluster::get_static_tlb_writer(const ChipId chip, const CoreCoord core) {
    tt_xy_pair translated_core = get_chip(chip)->translate_chip_coord_to_translated(core);
    return get_tlb_manager(chip)->get_static_tlb_writer(translated_core);
}

std::map<int, int> Cluster::get_clocks() {
    std::map<int, int> clock_freq_map;
    for (auto& chip_id : local_chip_ids_) {
        clock_freq_map.insert({chip_id, chips_.at(chip_id)->get_clock()});
    }
    return clock_freq_map;
}

Cluster::~Cluster() {
    log_debug(LogUMD, "Cluster::~Cluster");

    cluster_desc.reset();
}

tlb_configuration Cluster::get_tlb_configuration(const ChipId chip, CoreCoord core) {
    tt_xy_pair translated_core = get_chip(chip)->translate_chip_coord_to_translated(core);
    return get_tlb_manager(chip)->get_tlb_configuration(translated_core);
}

// TODO: These configure_tlb APIs are soon going away.
void Cluster::configure_tlb(
    ChipId logical_device_id, tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) {
    configure_tlb(
        logical_device_id,
        get_soc_descriptor(logical_device_id).get_coord_at(core, CoordSystem::TRANSLATED),
        tlb_size,
        address,
        ordering);
}

void Cluster::configure_tlb(
    ChipId logical_device_id, CoreCoord core, size_t tlb_size, uint64_t address, uint64_t ordering) {
    tt_xy_pair translated_core = get_chip(logical_device_id)->translate_chip_coord_to_translated(core);
    get_tlb_manager(logical_device_id)->configure_tlb(translated_core, tlb_size, address, ordering);
}

void* Cluster::host_dma_address(std::uint64_t offset, ChipId src_device_id, uint16_t channel) const {
    HugepageMapping hugepage_map = get_chip(src_device_id)->get_sysmem_manager()->get_hugepage_mapping(channel);
    if (hugepage_map.mapping != nullptr) {
        return static_cast<std::byte*>(hugepage_map.mapping) + offset;
    } else {
        return nullptr;
    }
}

TTDevice* Cluster::get_tt_device(ChipId device_id) const {
    auto tt_device = get_chip(device_id)->get_tt_device();
    TT_ASSERT(tt_device != nullptr, "TTDevice not found for device: {}", device_id);
    return tt_device;
}

TLBManager* Cluster::get_tlb_manager(ChipId device_id) const { return get_chip(device_id)->get_tlb_manager(); }

Chip* Cluster::get_chip(ChipId device_id) const {
    auto chip_it = chips_.find(device_id);
    TT_ASSERT(chip_it != chips_.end(), "Device id {} not found in cluster.", device_id);
    return chip_it->second.get();
}

LocalChip* Cluster::get_local_chip(ChipId device_id) const {
    TT_ASSERT(local_chip_ids_.find(device_id) != local_chip_ids_.end(), "Device id {} is not a local chip.", device_id);
    return dynamic_cast<LocalChip*>(get_chip(device_id));
}

RemoteChip* Cluster::get_remote_chip(ChipId device_id) const {
    TT_ASSERT(
        remote_chip_ids_.find(device_id) != remote_chip_ids_.end(), "Device id {} is not a remote chip.", device_id);
    return dynamic_cast<RemoteChip*>(get_chip(device_id));
}

void Cluster::wait_for_non_mmio_flush(const ChipId chip_id) { get_chip(chip_id)->wait_for_non_mmio_flush(); }

void Cluster::wait_for_non_mmio_flush() {
    for (auto& [chip_id, chip] : chips_) {
        chip->wait_for_non_mmio_flush();
    }
}

std::unordered_map<ChipId, std::vector<std::vector<int>>>& Cluster::get_ethernet_broadcast_headers(
    const std::set<ChipId>& chips_to_exclude) {
    // Generate headers for Ethernet Broadcast (WH) only. Each header corresponds to a unique broadcast "grid".
    if (bcast_header_cache.find(chips_to_exclude) == bcast_header_cache.end()) {
        bcast_header_cache[chips_to_exclude] = {};
        std::unordered_map<ChipId, std::unordered_map<ChipId, std::vector<int>>>
            broadcast_mask_for_target_chips_per_group = {};
        std::map<std::vector<int>, std::tuple<ChipId, std::vector<int>>> broadcast_header_union_per_group = {};
        ChipId first_mmio_chip = *(get_target_mmio_device_ids().begin());
        for (const auto& chip : all_chip_ids_) {
            if (chips_to_exclude.find(chip) == chips_to_exclude.end()) {
                // Get shelf local physical chip id included in broadcast.
                ChipId physical_chip_id = cluster_desc->get_shelf_local_physical_chip_coords(chip);
                EthCoord eth_coords = cluster_desc->get_chip_locations().at(chip);
                // Rack word to be set in header.
                uint32_t rack_word = eth_coords.rack >> 2;
                // Rack byte to be set in header.
                uint32_t rack_byte = eth_coords.rack % 4;
                // 1st level grouping: Group broadcasts based on the MMIO chip they must go through
                // Nebula + Galaxy Topology assumption: Disjoint sets can only be present in the first shelf, with each
                // set connected to host through its closest MMIO chip For the first shelf, pass broadcasts to specific
                // chips through their closest MMIO chip All other shelves are fully connected galaxy grids. These are
                // connected to all MMIO devices. Use any (or the first) MMIO device in the list.
                ChipId closest_mmio_chip = 0;
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
                    // Target was seen before -> include curr rack and shelf in header.
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
                // Generate a hash for this MMIO Chip + Rack + Shelf group.
                std::vector<int> header_hash = {
                    mmio_group.first, chip.second.at(0), chip.second.at(1), chip.second.at(2)};
                if (broadcast_header_union_per_group.find(header_hash) == broadcast_header_union_per_group.end()) {
                    broadcast_header_union_per_group.insert(
                        {header_hash, std::make_tuple(mmio_group.first, chip.second)});
                } else {
                    // If group found, update chip header entry.
                    std::get<1>(broadcast_header_union_per_group.at(header_hash)).at(3) |= chip.second.at(3);
                }
            }
        }
        // Get all broadcast headers per MMIO group.
        for (const auto& header : broadcast_header_union_per_group) {
            ChipId mmio_chip = std::get<0>(header.second);
            if (bcast_header_cache[chips_to_exclude].find(mmio_chip) == bcast_header_cache[chips_to_exclude].end()) {
                bcast_header_cache[chips_to_exclude].insert({mmio_chip, {}});
            }
            bcast_header_cache[chips_to_exclude].at(mmio_chip).push_back(std::get<1>(header.second));
        }
        // Invert headers (FW convention).
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
    const std::set<uint32_t>& cols_to_exclude, const architecture_implementation* architecture_implementation) {
    bool found_tensix_or_eth = false;
    for (const auto& col : architecture_implementation->get_t6_x_locations()) {
        found_tensix_or_eth |= (cols_to_exclude.find(col) == cols_to_exclude.end());
    }
    return found_tensix_or_eth;
}

inline bool valid_tensix_broadcast_grid(
    const std::set<uint32_t>& rows_to_exclude,
    const std::set<uint32_t>& cols_to_exclude,
    const architecture_implementation* architecture_implementation) {
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
    const std::set<ChipId>& chips_to_exclude,
    const std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& cols_to_exclude,
    bool use_translated_coords) {
    if (use_ethernet_broadcast) {
        // Broadcast through ERISC core supported.
        std::unordered_map<ChipId, std::vector<std::vector<int>>>& broadcast_headers =
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
                header.at(4) = use_translated_coords * 0x8000;  // Reset row/col exclusion masks
                header.at(4) |= row_exclusion_mask;
                header.at(4) |= col_exclusion_mask;
                get_local_chip(mmio_group.first)->ethernet_broadcast_write(mem_ptr, address, size_in_bytes, header);
            }
        }
    } else {
        // Broadcast not supported. Implement this at the software level as a for loop.
        for (const auto& chip : all_chip_ids_) {
            if (chips_to_exclude.find(chip) != chips_to_exclude.end()) {
                continue;
            }
            for (const CoreCoord core : get_soc_descriptor(chip).get_all_cores(CoordSystem::TRANSLATED)) {
                if (cols_to_exclude.find(core.x) == cols_to_exclude.end() &&
                    rows_to_exclude.find(core.y) == rows_to_exclude.end()) {
                    write_to_device(mem_ptr, size_in_bytes, chip, core, address);
                }
            }
        }
    }
}

void Cluster::broadcast_write_to_cluster(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<ChipId>& chips_to_exclude,
    std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& cols_to_exclude) {
    if (arch_name == tt::ARCH::BLACKHOLE) {
        auto architecture_implementation = architecture_implementation::create(arch_name);
        if (cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(9) == cols_to_exclude.end()) {
            TT_ASSERT(
                !tensix_or_eth_in_broadcast(cols_to_exclude, architecture_implementation.get()),
                "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Blackhole.");
            if (cols_to_exclude.find(0) == cols_to_exclude.end()) {
                // When broadcast includes column zero do not exclude anything.
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
                    false);
            }
        } else {
            TT_ASSERT(
                use_translated_coords_for_eth_broadcast or
                    valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()),
                "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(
                mem_ptr,
                size_in_bytes,
                address,
                chips_to_exclude,
                rows_to_exclude,
                cols_to_exclude,
                use_translated_coords_for_eth_broadcast);
        }
    } else {
        auto architecture_implementation = architecture_implementation::create(arch_name);
        if (cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(5) == cols_to_exclude.end()) {
            TT_ASSERT(
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
                    false);
            }
        } else {
            TT_ASSERT(
                use_translated_coords_for_eth_broadcast or
                    valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()),
                "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(
                mem_ptr,
                size_in_bytes,
                address,
                chips_to_exclude,
                rows_to_exclude,
                cols_to_exclude,
                use_translated_coords_for_eth_broadcast);
        }
    }
}

void Cluster::write_to_sysmem(
    const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, ChipId src_device_id) {
    get_chip(src_device_id)->write_to_sysmem(channel, mem_ptr, addr, size);
}

void Cluster::read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, ChipId src_device_id) {
    get_chip(src_device_id)->read_from_sysmem(channel, mem_ptr, addr, size);
}

void Cluster::l1_membar(const ChipId chip, const std::unordered_set<CoreCoord>& cores) {
    get_chip(chip)->l1_membar(cores);
}

void Cluster::dram_membar(const ChipId chip, const std::unordered_set<CoreCoord>& cores) {
    get_chip(chip)->dram_membar(cores);
}

void Cluster::dram_membar(const ChipId chip, const std::unordered_set<uint32_t>& channels) {
    get_chip(chip)->dram_membar(channels);
}

void Cluster::write_to_device(const void* mem_ptr, uint32_t size_in_bytes, ChipId chip, CoreCoord core, uint64_t addr) {
    get_chip(chip)->write_to_device(core, mem_ptr, addr, size_in_bytes);
}

void Cluster::write_to_device_reg(
    const void* mem_ptr, uint32_t size_in_bytes, ChipId chip, CoreCoord core, uint64_t addr) {
    get_chip(chip)->write_to_device_reg(core, mem_ptr, addr, size_in_bytes);
}

void Cluster::dma_write_to_device(const void* src, size_t size, ChipId chip, CoreCoord core, uint64_t addr) {
    get_chip(chip)->dma_write_to_device(src, size, core, addr);
}

void Cluster::dma_read_from_device(void* dst, size_t size, ChipId chip, CoreCoord core, uint64_t addr) {
    get_chip(chip)->dma_read_from_device(dst, size, core, addr);
}

void Cluster::read_from_device(void* mem_ptr, ChipId chip, CoreCoord core, uint64_t addr, uint32_t size) {
    get_chip(chip)->read_from_device(core, mem_ptr, addr, size);
}

void Cluster::read_from_device_reg(void* mem_ptr, ChipId chip, CoreCoord core, uint64_t addr, uint32_t size) {
    get_chip(chip)->read_from_device_reg(core, mem_ptr, addr, size);
}

void Cluster::noc_multicast_write(
    void* dst, size_t size, ChipId chip, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    get_chip(chip)->noc_multicast_write(dst, size, core_start, core_end, addr);
}

int Cluster::arc_msg(
    int logical_device_id,
    uint32_t msg_code,
    bool wait_for_done,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    return get_chip(logical_device_id)->arc_msg(msg_code, wait_for_done, args, timeout_ms, return_3, return_4);
}

void Cluster::broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions& soft_resets) {
    if (chips_.empty()) {
        // Nowhere to broadcast to.
        return;
    }
    // If ethernet broadcast is not supported, do it one by one.
    if (!use_ethernet_broadcast) {
        for (auto& chip_id : all_chip_ids_) {
            get_chip(chip_id)->send_tensix_risc_reset(soft_resets);
        }
        return;
    }

    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type)valid;
    std::set<ChipId> chips_to_exclude = {};
    std::set<uint32_t> rows_to_exclude;
    std::set<uint32_t> columns_to_exclude;
    if (arch_name == tt::ARCH::BLACKHOLE) {
        rows_to_exclude = {0, 1};
        columns_to_exclude = {0, 8, 9};
    } else {
        rows_to_exclude = {0, 6};
        columns_to_exclude = {0, 5};
    }
    broadcast_write_to_cluster(
        &valid_val, sizeof(uint32_t), 0xFFB121B0, chips_to_exclude, rows_to_exclude, columns_to_exclude);
    // Ensure that reset signal is globally visible.
    wait_for_non_mmio_flush();
}

void Cluster::set_power_state(DevicePowerState device_state) {
    for (auto& [_, chip] : chips_) {
        chip->set_power_state(device_state);
    }
}

void Cluster::deassert_resets_and_set_power_state() {
    // Assert tensix resets on all chips in cluster.
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);

    for (auto& [_, chip] : chips_) {
        chip->deassert_risc_resets();
    }

    // MT Initial BH - ARC messages not supported in Blackhole.
    if (arch_name != tt::ARCH::BLACKHOLE && arch_name != tt::ARCH::QUASAR) {
        for (const ChipId& chip : all_chip_ids_) {
            get_chip(chip)->enable_ethernet_queue();
        }
    }

    // Set power state to busy.
    set_power_state(DevicePowerState::BUSY);
}

void Cluster::start_device(const DeviceParams& device_params) {
    if (device_params.init_device) {
        for (auto chip_id : all_chip_ids_) {
            get_chip(chip_id)->start_device();
        }

        deassert_resets_and_set_power_state();
    }
}

void Cluster::close_device() {
    // Close remote device first because sending risc reset requires corresponding pcie device to be active.
    for (auto remote_chip_id : remote_chip_ids_) {
        get_chip(remote_chip_id)->close_device();
    }

    for (auto chip_id : local_chip_ids_) {
        get_chip(chip_id)->close_device();
    }
}

std::uint32_t Cluster::get_num_host_channels(std::uint32_t device_id) {
    return chips_.at(device_id)->get_num_host_channels();
}

std::uint32_t Cluster::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    return chips_.at(device_id)->get_host_channel_size(channel);
}

std::uint32_t Cluster::get_numa_node_for_pcie_device(std::uint32_t device_id) {
    return chips_.at(device_id)->get_numa_node();
}

std::uint64_t Cluster::get_pcie_base_addr_from_device(const ChipId chip_id) const {
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

std::optional<semver_t> Cluster::get_ethernet_firmware_version() const { return eth_fw_version; }

std::optional<semver_t> Cluster::get_firmware_bundle_version() const { return fw_bundle_version; }

void Cluster::set_barrier_address_params(const BarrierAddressParams& barrier_address_params) {
    for (auto& [_, chip] : chips_) {
        chip->set_barrier_address_params(barrier_address_params);
    }
}

std::unique_ptr<ClusterDescriptor> Cluster::create_cluster_descriptor(
    std::string sdesc_path, IODeviceType device_type) {
    TopologyDiscoveryOptions options;
    options.soc_descriptor_path = sdesc_path;
    options.io_device_type = device_type;
    return TopologyDiscovery::discover(std::move(options)).first;
}

}  // namespace tt::umd
