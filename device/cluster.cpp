// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "api/umd/device/cluster.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hugepage.hpp"
#include "tracy.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/chip/mock_chip.hpp"
#include "umd/device/chip/remote_chip.hpp"
// Simulation-specific headers -- only needed when TT_UMD_BUILD_SIMULATION is set.
// The code that uses these types is guarded by #ifdef TT_UMD_BUILD_SIMULATION below.
#ifdef TT_UMD_BUILD_SIMULATION
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#endif  // TT_UMD_BUILD_SIMULATION
// SWEmuleChip is only referenced inside `#ifdef TT_UMD_BUILD_EMULE`. IWYU
// runs without that flag set so it can't see the use; mark the include to
// stop future IWYU sweeps from deleting it again (see #2536).
#include "umd/device/chip/sw_emule_chip.hpp"  // IWYU pragma: keep
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/ethernet_broadcast.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {
class TlbWindow;

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

void Cluster::log_device_summary() {
    switch (cluster_desc->get_io_device_type()) {
        case IODeviceType::PCIe:
            log_pci_device_summary();
            break;
        case IODeviceType::JTAG:
            // Currently no specific device logging needed for JTAG.
            break;
        default:
            UMD_THROW(error::RuntimeError, "Unknown device type for logging.");
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
    ZoneScopedC(tracy::Color::DarkGreen);
    // TODO: work on removing this member altogether. Currently assumes all have the same arch.
    arch_name = chips_.empty() ? tt::ARCH::Invalid : chips_.begin()->second->get_soc_descriptor().arch;

    eth_fw_version = cluster_desc->get_cluster_eth_fw_version();

    if (chip_type == ChipType::SILICON) {
        std::vector<int> pci_ids;
        auto mmio_id_map = cluster_desc->get_chips_with_mmio();
        pci_ids.reserve(local_chip_ids_.size());
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
    }

    // Translated coordinates can be used for broadcast headers if NOC translation is enabled.
    use_translated_coords_for_eth_broadcast = true;
    for (const auto& chip : all_chip_ids_) {
        use_translated_coords_for_eth_broadcast &= get_soc_descriptor(chip).noc_translation_enabled;
    }

    // Disable dependency to ethernet firmware for all BH devices and WH devices with all chips having MMIO (e.g. UBB
    // Galaxy, or P300).
    // The ethernet firmware also requires host memory for broadcasting.
    bool use_ethernet_broadcast = chip_type == ChipType::SILICON && arch_name == tt::ARCH::WORMHOLE_B0 &&
                                  !remote_chip_ids_.empty() && num_host_mem_ch_per_mmio_device > 0;
    if (use_ethernet_broadcast) {
        ChipId first_mmio_chip = *local_chip_ids_.begin();
        std::unordered_map<ChipId, ChipId> chip_to_mmio_chip;
        for (const ChipId chip : all_chip_ids_) {
            const EthCoord& eth_coords = cluster_desc->get_chip_locations().at(chip);
            chip_to_mmio_chip[chip] = (eth_coords.rack == 0 && eth_coords.shelf == 0)
                                          ? cluster_desc->get_closest_mmio_capable_chip(chip)
                                          : first_mmio_chip;
        }
        std::unordered_map<ChipId, RemoteCommunication*> mmio_remote_comms;
        for (auto& [chip_id, rc] : remote_communications_) {
            mmio_remote_comms[chip_id] = rc.get();
        }
        ethernet_broadcast_ = std::make_unique<EthernetBroadcast>(
            arch_name, cluster_desc->get_chip_locations(), chip_to_mmio_chip, mmio_remote_comms);
    }
}

std::unique_ptr<Chip> Cluster::construct_chip_from_cluster(
    ChipId chip_id,
    const ChipType& chip_type,
    ClusterDescriptor* cluster_desc,
    SocDescriptor& soc_desc,
    int num_host_mem_channels,
    const std::filesystem::path& simulator_directory,
    std::unique_ptr<TTDevice> tt_device) {
    if (chip_type == ChipType::MOCK) {
        return std::make_unique<MockChip>(soc_desc);
    }
    if (chip_type == ChipType::SWEMULE) {
#ifdef TT_UMD_BUILD_EMULE
        return std::make_unique<SWEmuleChip>(soc_desc);
#else
        throw std::runtime_error(
            "SWEMULE device is not supported in this build. Set '-DTT_UMD_BUILD_EMULE=ON' during cmake "
            "configuration to enable software emulation device.");
#endif
    }
    if (chip_type == ChipType::SIMULATION) {
#ifdef TT_UMD_BUILD_SIMULATION
        log_info(LogUMD, "Creating Simulation device");
        return SimulationChip::create(
            simulator_directory, soc_desc, chip_id, cluster_desc->get_number_of_chips(), num_host_mem_channels);
#else
        UMD_THROW(
            error::RuntimeError,
            "Simulation device is not supported in this build. Set '-DTT_UMD_BUILD_SIMULATION=ON' during cmake "
            "configuration to enable simulation device.");
#endif
    }

    if (cluster_desc->is_chip_mmio_capable(chip_id)) {
        std::unique_ptr<LocalChip> chip;
        chip = LocalChip::create(std::move(tt_device), soc_desc, num_host_mem_channels);

        if (cluster_desc->get_arch(chip_id) == tt::ARCH::WORMHOLE_B0) {
            // Remote transfer currently supported only for wormhole.
            SysmemManager* sysmem_ptr = chip->get_sysmem_manager();
            if (sysmem_ptr != nullptr && sysmem_ptr->get_num_host_mem_channels() == 0) {
                sysmem_ptr = nullptr;
            }
            remote_communications_[chip_id] =
                RemoteCommunication::create_remote_communication(chip->get_tt_device(), {0, 0, 0, 0}, sysmem_ptr);
            remote_communications_[chip_id]->set_remote_transfer_ethernet_cores(
                chip->get_soc_descriptor().get_eth_xy_pairs_for_channels(
                    cluster_desc->get_active_eth_channels(chip_id), CoordSystem::TRANSLATED));
        }
        return chip;
    } else {
        ChipId gateway_id = cluster_desc->get_closest_mmio_capable_chip(chip_id);
        return RemoteChip::create(
            get_local_chip(gateway_id),
            cluster_desc->get_chip_location(chip_id),
            cluster_desc->get_active_eth_channels(gateway_id),
            soc_desc);
    }
}

SocDescriptor Cluster::construct_soc_descriptor(
    const std::string& soc_desc_path, ChipId chip_id, ChipType chip_type, ClusterDescriptor* cluster_desc) {
    ZoneScopedC(tracy::Color::DarkGreen);

    bool chip_in_cluster_descriptor =
        cluster_desc->get_all_chips().find(chip_id) != cluster_desc->get_all_chips().end();

    // In case of SILICON chip type, this chip has to exist in the cluster descriptor. But it doesn't have to exist in
    // case of Mock or Simulation chip type.
    if (chip_type == ChipType::SILICON && !chip_in_cluster_descriptor) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Chip {} not found in cluster descriptor. Cannot create device.", chip_id));
    }

    ChipInfo chip_info;
    if (chip_in_cluster_descriptor) {
        chip_info.noc_translation_enabled = cluster_desc->get_noc_translation_table_en().at(chip_id);
        chip_info.harvesting_masks = HarvestingMasks{};
        chip_info.harvesting_masks = cluster_desc->get_harvesting_masks(chip_id);
        chip_info.board_type = cluster_desc->get_board_type(chip_id);
        chip_info.asic_location = cluster_desc->get_asic_location(chip_id);
    }

    log_debug(
        LogUMD,
        "Harvesting masks for Chip {}: Tensix: {:#x} DRAM: {:#x} ETH: {:#x} PCIe: {:#x} L2CPU: {:#x}",
        chip_id,
        chip_info.harvesting_masks.tensix_harvesting_mask,
        chip_info.harvesting_masks.dram_harvesting_mask,
        chip_info.harvesting_masks.eth_harvesting_mask,
        chip_info.harvesting_masks.pcie_harvesting_mask,
        chip_info.harvesting_masks.l2cpu_harvesting_mask);

    if (soc_desc_path.empty()) {
        tt::ARCH arch = chip_in_cluster_descriptor ? cluster_desc->get_arch(chip_id) : tt::ARCH::WORMHOLE_B0;

        SocDescriptor soc_descriptor = SocDescriptor(std::make_shared<SocArchDescriptor>(arch), chip_info);
        return soc_descriptor;

    } else {
        SocDescriptor soc_descriptor = SocDescriptor(std::make_shared<SocArchDescriptor>(soc_desc_path), chip_info);

        // In this case, check that the passed soc descriptor architecture doesn't conflate with the one in the cluster
        // descriptor.
        if (chip_in_cluster_descriptor && soc_descriptor.arch != cluster_desc->get_arch(chip_id)) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Passed SOC descriptor has {} architecture, but Chip ID {} has {} architecture.",
                    arch_to_str(soc_descriptor.arch),
                    chip_id,
                    arch_to_str(cluster_desc->get_arch(chip_id))));
        }

        return soc_descriptor;
    }
}

void Cluster::add_chip(const ChipId& chip_id, const ChipType& chip_type, std::unique_ptr<Chip> chip) {
    UMD_ASSERT(
        chips_.find(chip_id) == chips_.end(),
        error::RuntimeError,
        fmt::format("Chip with id {} already exists in cluster. Cannot add another chip with the same id.", chip_id));
    all_chip_ids_.insert(chip_id);
    // All non silicon chip types are considered local chips.
    if (chip_type == ChipType::SIMULATION || chip_type == ChipType::SWEMULE ||
        cluster_desc->is_chip_mmio_capable(chip_id)) {
        local_chip_ids_.insert(chip_id);
    } else {
        remote_chip_ids_.insert(chip_id);
    }
    chips_.emplace(chip_id, std::move(chip));
}

// Options is intentionally taken by value because it may be mutated when TT_UMD_BUILD_SIMULATION is enabled.
Cluster::Cluster(ClusterOptions options) {
    ZoneScopedNC("Cluster::Cluster", tracy::Color::DarkGreen);
    log_info(LogUMD, "Cluster constructor started.");
    // Store options early so that options_ is populated if the constructor throws.
    // A second assignment at the end captures any mutations made during construction
    // (sdesc_path resolution, num_host_mem_ch_per_mmio_device auto-detect).
    options_ = options;
    std::map<ChipId, std::unique_ptr<TTDevice>> tt_devices;
    switch (options.chip_type) {
        case ChipType::SILICON: {
            if (options.cluster_descriptor != nullptr) {
                UMD_THROW(error::RuntimeError, "Cannot pass a custom ClusterDescriptor for SILICON chip type.");
            }

            auto [desc, devices] = TopologyDiscovery::discover(
                options.topology_discovery_options, options.io_device_type, options.sdesc_path);
            cluster_desc = std::move(desc);
            tt_devices = std::move(devices);
            break;
        }
        case ChipType::MOCK:
        case ChipType::SWEMULE:
        case ChipType::SIMULATION: {
            if (options.cluster_descriptor == nullptr) {
                // If no custom descriptor is provided, in case of mock or simulation chip type, we create a mock
                // cluster descriptor from passed target devices.
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
                bool is_ttsim_simulation =
                    (options.chip_type == ChipType::SIMULATION && options.simulator_directory.extension() == ".so");
                bool noc_translation_enabled = options.chip_type == ChipType::MOCK ||
                                               options.chip_type == ChipType::SWEMULE || is_ttsim_simulation;
                std::unique_ptr<ClusterDescriptor> temp_full_cluster_desc_ptr =
                    ClusterDescriptor::create_mock_cluster(options.target_devices, arch, noc_translation_enabled);

                cluster_desc = ClusterDescriptor::create_constrained_cluster_descriptor(
                    temp_full_cluster_desc_ptr.get(), options.target_devices);

                break;
            }

            cluster_desc = ClusterDescriptor::create_constrained_cluster_descriptor(
                options.cluster_descriptor, options.target_devices);

            break;
        }
        default:
            UMD_THROW(error::RuntimeError, "Unsupported chip type.");
    }

    if (!options.num_host_mem_ch_per_mmio_device.has_value()) {
        auto grouped_chips = cluster_desc->get_chips_grouped_by_closest_mmio();
        uint32_t max_chips_per_mmio = 0;
        for (const auto& [mmio_device_id, chips] : grouped_chips) {
            max_chips_per_mmio = std::max(max_chips_per_mmio, static_cast<uint32_t>(chips.size()));
        }
        options.num_host_mem_ch_per_mmio_device = std::min(MAX_HOST_MEM_CHANNELS, max_chips_per_mmio);
        log_debug(LogUMD, "Set number of host memory channels to {}.", options.num_host_mem_ch_per_mmio_device.value());
    }

    // Construct all the required chips from the cluster descriptor.
    for (auto& chip_id : cluster_desc->get_chips_local_first(cluster_desc->get_all_chips())) {
        SocDescriptor soc_desc =
            construct_soc_descriptor(options.sdesc_path, chip_id, options.chip_type, cluster_desc.get());

        // Reuse TTDevice from topology discovery if available, avoiding duplicate device creation.
        std::unique_ptr<TTDevice> tt_device;
        auto it = tt_devices.find(chip_id);
        if (it != tt_devices.end()) {
            tt_device = std::move(it->second);
            tt_devices.erase(it);
        }

        add_chip(
            chip_id,
            options.chip_type,
            construct_chip_from_cluster(
                chip_id,
                options.chip_type,
                cluster_desc.get(),
                soc_desc,
                options.num_host_mem_ch_per_mmio_device.value(),
                options.simulator_directory,
                std::move(tt_device)));
    }

#ifdef TT_UMD_BUILD_SIMULATION
    // -------------------------------------------------------------------
    // Multichip eth-MAC wiring pre-pass.
    // For SIMULATION ChipType with multichip-aware libttsim, populate the virtual
    // switch routing table and pre-write peer DEST_MAC into each eth tile so
    // that firmware sees correctly wired neighbours at boot time.
    // -------------------------------------------------------------------
    if (options.chip_type == ChipType::SIMULATION && options.simulator_directory.extension() == ".so") {
        // Walk chips_ and return the TTSimCommunicator for a given chip.
        auto get_comm = [&](ChipId cid) -> tt::umd::TTSimCommunicator* {
            auto it = chips_.find(cid);
            if (it == chips_.end()) {
                return nullptr;
            }
            auto* sim_chip = dynamic_cast<tt::umd::SimulationChip*>(it->second.get());
            if (!sim_chip) {
                return nullptr;
            }
            auto* sim_tt = dynamic_cast<tt::umd::TTSimTTDevice*>(sim_chip->get_tt_device());
            return sim_tt ? sim_tt->get_communicator() : nullptr;
        };
        // Deterministic per-(chip, channel) MAC: top byte is OUI, next byte
        // encodes chip_id, bottom byte encodes channel.
        auto eth_sim_mac = [](ChipId cid, int chan) -> uint64_t {
            UMD_ASSERT(
                cid <= 0xFF && chan <= 0xFF,
                error::RuntimeError,
                fmt::format("MAC encoding overflow: chip_id {} channel {}", cid, chan));
            return 0x021E52000000ULL | (uint64_t(cid & 0xFF) << 8) | uint64_t(chan & 0xFF);
        };
        // Reset the virtual switch once (singleton, process-global state).
        if (!chips_.empty()) {
            ChipId first_chip_id = cluster_desc->get_chips_local_first(cluster_desc->get_all_chips()).front();
            if (auto* first_chip_comm = get_comm(first_chip_id)) {
                first_chip_comm->switch_reset();
            }
        }
        // For every connected eth pair (chip_a:chan_a <-> chip_b:chan_b),
        // register MACs and peer handles.  Process each undirected edge once
        // (chip_a < chip_b) to avoid double-registration.
        const auto& eth_conns = cluster_desc->get_ethernet_connections();
        for (const auto& [chip_a, chan_map] : eth_conns) {
            for (const auto& [chan_a, remote] : chan_map) {
                ChipId chip_b = std::get<0>(remote);
                int chan_b = std::get<1>(remote);
                if (chip_a >= chip_b) {
                    continue;
                }
                auto* comm_a = get_comm(chip_a);
                auto* comm_b = get_comm(chip_b);
                if (!comm_a || !comm_b) {
                    continue;
                }
                uint64_t mac_a = eth_sim_mac(chip_a, chan_a);
                uint64_t mac_b = eth_sim_mac(chip_b, chan_b);
                comm_a->register_eth_endpoint(uint32_t(chan_a), mac_a);
                comm_b->register_eth_endpoint(uint32_t(chan_b), mac_b);
                // Wire bidirectional peer info for source-aware routing (BH BCAST/MCAST MAC).
                comm_a->register_peer(uint32_t(chan_a), comm_b->get_dev_handle(), uint32_t(chan_b));
                comm_b->register_peer(uint32_t(chan_b), comm_a->get_dev_handle(), uint32_t(chan_a));
                log_info(
                    tt::LogEmulationDriver,
                    "TTSim eth: {}:ch{} mac={:#014x}  <->  {}:ch{} mac={:#014x}",
                    chip_a,
                    chan_a,
                    mac_a,
                    chip_b,
                    chan_b,
                    mac_b);
            }
        }
    }
#endif  // TT_UMD_BUILD_SIMULATION

    construct_cluster(options.num_host_mem_ch_per_mmio_device.value(), options.chip_type);
    // Overwrite with the final (possibly mutated) options: sdesc_path may have been
    // resolved and num_host_mem_ch_per_mmio_device auto-detected above.
    options_ = std::move(options);
    log_info(LogUMD, "Cluster constructor completed.");
}

#ifdef TT_UMD_BUILD_SIMULATION
// Passthroughs to libttsim_switch_register_fabric_* -- allow tt-metal fabric
// init to wire mock multichip topologies under craq-sim.

void Cluster::register_sim_fabric_endpoint_direction(ChipId chip_id, uint32_t eth_tile_id, uint32_t direction) {
    auto it = chips_.find(chip_id);
    if (it == chips_.end()) {
        return;
    }
    auto* sim_chip = dynamic_cast<SimulationChip*>(it->second.get());
    if (!sim_chip) {
        return;
    }
    auto* sim_tt = dynamic_cast<TTSimTTDevice*>(sim_chip->get_tt_device());
    if (!sim_tt) {
        return;
    }
    if (auto* communicator = sim_tt->get_communicator()) {
        communicator->register_fabric_endpoint_direction(eth_tile_id, direction);
    }
}

void Cluster::register_sim_fabric_node_id(ChipId chip_id, uint32_t mesh_id, uint32_t fabric_chip_id) {
    auto chip_it = chips_.find(chip_id);
    if (chip_it == chips_.end()) {
        return;
    }
    auto* sim_chip = dynamic_cast<SimulationChip*>(chip_it->second.get());
    if (!sim_chip) {
        return;
    }
    auto* sim_tt = dynamic_cast<TTSimTTDevice*>(sim_chip->get_tt_device());
    if (!sim_tt) {
        return;
    }
    if (auto* communicator = sim_tt->get_communicator()) {
        communicator->register_fabric_node_id(mesh_id, fabric_chip_id);
    }
}
#endif  // TT_UMD_BUILD_SIMULATION

void Cluster::configure_active_ethernet_cores_for_mmio_device(
    ChipId mmio_chip, const std::unordered_set<CoreCoord>& active_eth_cores_per_chip) {
    // Remote communication is only set up on architectures that support it (currently Wormhole). On other
    // architectures there is nothing to configure for remote ethernet transfers.
    if (remote_communications_.find(mmio_chip) == remote_communications_.end()) {
        return;
    }
    // The ethernet cores that should be used for remote transfer are set in the RemoteCommunication structure.
    // This structure is used by remote chips. So we need to find all remote chips that use the passed in mmio_chip,
    // and set the active ethernet cores for them.
    for (const auto& remote_chip_id : remote_chip_ids_) {
        if (cluster_desc->get_closest_mmio_capable_chip(remote_chip_id) == mmio_chip) {
            get_remote_chip(remote_chip_id)->set_remote_transfer_ethernet_cores(active_eth_cores_per_chip);
        }
    }
    // Local chips hold communication primitives for broadcasting, so we have to set this up for them as well.
    remote_communications_.at(mmio_chip)->set_remote_transfer_ethernet_cores(
        get_local_chip(mmio_chip)->get_soc_descriptor().translate_coords_to_xy_pair(
            active_eth_cores_per_chip, CoordSystem::TRANSLATED));
}

std::set<ChipId> Cluster::get_target_device_ids() { return all_chip_ids_; }

std::set<ChipId> Cluster::get_target_mmio_device_ids() { return local_chip_ids_; }

std::set<ChipId> Cluster::get_target_remote_device_ids() { return remote_chip_ids_; }

void Cluster::assert_risc_reset() {
    // Workaround for quasar. Broadcast reset is not supported for quasar so we need to
    // loop all chips and issues the reset separately.
    if (arch_name == tt::ARCH::QUASAR) {
        for (const auto& chip : all_chip_ids_) {
            get_chip(chip)->assert_risc_reset(RiscType::ALL);
        }
        return;
    }

    uint32_t reset_reg_value =
        architecture_implementation::create(arch_name)->get_soft_reset_reg_value(RiscType::ALL_TENSIX);
    broadcast_tensix_risc_reset_to_cluster(reset_reg_value);
}

void Cluster::deassert_risc_reset() {
    // Workaround for quasar. Broadcast reset is not supported for quasar so we need to
    // loop all chips and issues the reset separately.
    if (arch_name == tt::ARCH::QUASAR) {
        for (const auto& chip : all_chip_ids_) {
            get_chip(chip)->deassert_risc_reset(RiscType::ALL, false);
        }
        return;
    }

    auto arch_impl = architecture_implementation::create(arch_name);
    uint32_t reset_reg_value = arch_impl->get_soft_reset_reg_value(RiscType::ALL_TENSIX & ~RiscType::BRISC) |
                               arch_impl->get_soft_reset_staggered_start();
    broadcast_tensix_risc_reset_to_cluster(reset_reg_value);
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

void Cluster::refresh_cluster_description() {
    if (options_.chip_type != ChipType::SILICON) {
        UMD_THROW(error::RuntimeError, "refresh_cluster_description() is only supported for SILICON chip type.");
    }
    if (options_.cluster_descriptor != nullptr) {
        UMD_THROW(
            error::RuntimeError,
            "refresh_cluster_description() is not supported when a custom cluster descriptor was provided.");
    }
    if (!options_.target_devices.empty()) {
        UMD_THROW(
            error::RuntimeError, "refresh_cluster_description() is not supported when target_devices is non-empty.");
    }

    // Build reverse map from unique ID to old chip ID before replacing the descriptor.
    const auto& old_unique_ids = cluster_desc->get_chip_unique_ids();
    std::unordered_map<uint64_t, ChipId> unique_id_to_old_chip_id;
    for (const auto& [chip_id, uid] : old_unique_ids) {
        unique_id_to_old_chip_id[uid] = chip_id;
    }

    auto new_cluster_desc = Cluster::create_cluster_descriptor(
        options_.sdesc_path, options_.io_device_type, options_.topology_discovery_options);

    // Validate that the same physical chips are present by matching unique IDs.
    const auto& new_unique_ids = new_cluster_desc->get_chip_unique_ids();
    if (new_unique_ids.size() != old_unique_ids.size()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "refresh_cluster_description: chip count changed from {} to {}. "
                "Recreate the Cluster to reflect hardware changes.",
                old_unique_ids.size(),
                new_unique_ids.size()));
    }

    for (const auto& [new_chip_id, uid] : new_unique_ids) {
        auto it = unique_id_to_old_chip_id.find(uid);
        if (it == unique_id_to_old_chip_id.end()) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "refresh_cluster_description: chip with unique ID 0x{:016x} is present in the new "
                    "cluster descriptor but not in the old one. Recreate the Cluster to reflect hardware changes.",
                    uid));
        }
        if (it->second != new_chip_id) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "refresh_cluster_description: chip ID changed from {} to {} (unique ID 0x{:016x}). "
                    "Recreate the Cluster to reflect hardware changes.",
                    it->second,
                    new_chip_id,
                    uid));
        }
    }

    cluster_desc = std::move(new_cluster_desc);
    eth_fw_version = cluster_desc->get_cluster_eth_fw_version();
    if (ethernet_broadcast_) {
        ChipId first_mmio_chip = *local_chip_ids_.begin();
        std::unordered_map<ChipId, ChipId> chip_to_mmio_chip;
        for (const ChipId chip : all_chip_ids_) {
            const EthCoord& eth_coords = cluster_desc->get_chip_locations().at(chip);
            chip_to_mmio_chip[chip] = (eth_coords.rack == 0 && eth_coords.shelf == 0)
                                          ? cluster_desc->get_closest_mmio_capable_chip(chip)
                                          : first_mmio_chip;
        }
        ethernet_broadcast_->clear_header_cache(cluster_desc->get_chip_locations(), chip_to_mmio_chip);
    }

    for (const ChipId chip_id : local_chip_ids_) {
        if (cluster_desc->get_arch(chip_id) == tt::ARCH::WORMHOLE_B0) {
            const std::set<uint32_t> active_channels = cluster_desc->get_active_eth_channels(chip_id);
            remote_communications_.at(chip_id)->set_remote_transfer_ethernet_cores(
                get_local_chip(chip_id)->get_soc_descriptor().get_eth_xy_pairs_for_channels(
                    active_channels, CoordSystem::TRANSLATED));
            for (const ChipId remote_chip_id : remote_chip_ids_) {
                if (cluster_desc->get_closest_mmio_capable_chip(remote_chip_id) == chip_id) {
                    get_remote_chip(remote_chip_id)->set_remote_transfer_ethernet_cores(active_channels);
                }
            }
        }
    }
}

TlbWindow* Cluster::get_static_tlb_window(const ChipId chip, const CoreCoord core) {
    tt_xy_pair translated_core = get_chip(chip)->get_soc_descriptor().translate_chip_coord_to_translated(core);
    return get_tlb_manager(chip)->get_tlb_window(translated_core);
}

std::map<int, int> Cluster::get_clocks() {
    std::map<int, int> clock_freq_map;
    for (auto& chip_id : local_chip_ids_) {
        clock_freq_map.insert({chip_id, chips_.at(chip_id)->get_clock()});
    }
    return clock_freq_map;
}

Cluster::~Cluster() {
    log_info(LogUMD, "Cluster destructor started.");

    cluster_desc.reset();
    log_info(LogUMD, "Cluster destructor completed.");
}

tlb_configuration Cluster::get_tlb_configuration(const ChipId chip, CoreCoord core) {
    tt_xy_pair translated_core = get_chip(chip)->get_soc_descriptor().translate_chip_coord_to_translated(core);
    return get_tlb_manager(chip)->get_tlb_configuration(translated_core);
}

// TODO: These configure_tlb APIs are soon going away.
void Cluster::configure_tlb(
    ChipId logical_device_id, tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) {
    ZoneScopedC(tracy::Color::Cyan);
    configure_tlb(
        logical_device_id,
        get_soc_descriptor(logical_device_id).get_coord_at(core, CoordSystem::TRANSLATED),
        tlb_size,
        address,
        ordering);
}

void Cluster::configure_tlb(
    ChipId logical_device_id, CoreCoord core, size_t tlb_size, uint64_t address, uint64_t ordering) {
    ZoneScopedC(tracy::Color::Cyan);
    tt_xy_pair translated_core =
        get_chip(logical_device_id)->get_soc_descriptor().translate_chip_coord_to_translated(core);
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
    UMD_ASSERT(tt_device != nullptr, error::RuntimeError, fmt::format("TTDevice not found for device: {}", device_id));
    return tt_device;
}

TLBManager* Cluster::get_tlb_manager(ChipId device_id) const { return get_chip(device_id)->get_tlb_manager(); }

Chip* Cluster::get_chip(ChipId device_id) const {
    auto chip_it = chips_.find(device_id);
    UMD_ASSERT(
        chip_it != chips_.end(), error::RuntimeError, fmt::format("Device id {} not found in cluster.", device_id));
    return chip_it->second.get();
}

LocalChip* Cluster::get_local_chip(ChipId device_id) const {
    UMD_ASSERT(
        local_chip_ids_.find(device_id) != local_chip_ids_.end(),
        error::RuntimeError,
        fmt::format("Device id {} is not a local chip.", device_id));
    return dynamic_cast<LocalChip*>(get_chip(device_id));
}

RemoteChip* Cluster::get_remote_chip(ChipId device_id) const {
    UMD_ASSERT(
        remote_chip_ids_.find(device_id) != remote_chip_ids_.end(),
        error::RuntimeError,
        fmt::format("Device id {} is not a remote chip.", device_id));
    return dynamic_cast<RemoteChip*>(get_chip(device_id));
}

void Cluster::wait_for_non_mmio_flush(const ChipId chip_id) { get_chip(chip_id)->wait_for_non_mmio_flush(); }

void Cluster::wait_for_non_mmio_flush() {
    for (auto& [chip_id, chip] : chips_) {
        chip->wait_for_non_mmio_flush();
    }
    for (auto& [chip_id, remote_comm] : remote_communications_) {
        remote_comm->wait_for_non_mmio_flush();
    }
}

void Cluster::broadcast_write_to_cluster(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<ChipId>& chips_to_exclude,
    std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& columns_to_exclude,
    bool use_translated_coords) {
    if (!ethernet_broadcast_) {
        // Broadcast not supported. Implement this at the software level as a for loop.
        // Only broadcast to TENSIX and DRAM cores (matching the supported destinations of the ERISC FW
        // broadcast). The row/col exclusion masks are bit positions in NOC0 space when
        // use_translated_coords is false, or in translated space when true.
        for (const auto& chip : all_chip_ids_) {
            if (chips_to_exclude.find(chip) != chips_to_exclude.end()) {
                continue;
            }
            if (use_translated_coords && arch_name == tt::ARCH::WORMHOLE_B0) {
                // Note that DRAM cores on Wormhole are mistakenly still reported as NOC0 even when TRANSLATED
                // coordinates are requested, so we need to manually adjust the exclusion list to include NOC0
                // coordinates of DRAM cores if they're requested in translated space.
                if (columns_to_exclude.find(16) != columns_to_exclude.end()) {
                    columns_to_exclude.insert(0);
                }
                if (columns_to_exclude.find(17) != columns_to_exclude.end()) {
                    columns_to_exclude.insert(5);
                }
            }
            for (const CoreType core_type : {CoreType::TENSIX, CoreType::DRAM}) {
                for (const CoreCoord core : get_soc_descriptor(chip).get_cores(
                         core_type, use_translated_coords ? CoordSystem::TRANSLATED : CoordSystem::NOC0)) {
                    if (columns_to_exclude.find(core.x) == columns_to_exclude.end() &&
                        rows_to_exclude.find(core.y) == rows_to_exclude.end()) {
                        write_to_device(mem_ptr, size_in_bytes, chip, core, address);
                    }
                }
            }
        }
        return;
    }

    ethernet_broadcast_->broadcast_write_to_cluster(
        mem_ptr, size_in_bytes, address, chips_to_exclude, rows_to_exclude, columns_to_exclude, use_translated_coords);
}

void Cluster::write_to_sysmem(
    const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, ChipId src_device_id) {
    get_chip(src_device_id)->write_to_sysmem(channel, mem_ptr, addr, size);
}

void Cluster::read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, ChipId src_device_id) {
    get_chip(src_device_id)->read_from_sysmem(channel, mem_ptr, addr, size);
}

void Cluster::advance_device_execution(ChipId device_id) { get_chip(device_id)->advance_device_execution(); }

void Cluster::l1_membar(const ChipId chip, const std::unordered_set<CoreCoord>& cores) {
    get_chip(chip)->l1_membar(cores);
}

void Cluster::dram_membar(const ChipId chip, const std::unordered_set<CoreCoord>& cores) {
    get_chip(chip)->dram_membar(cores);
}

void Cluster::dram_membar(const ChipId chip, const std::unordered_set<uint32_t>& channels, uint32_t subchannel) {
    get_chip(chip)->dram_membar(channels, subchannel);
}

void Cluster::write_to_device(const void* mem_ptr, size_t size_in_bytes, ChipId chip, CoreCoord core, uint64_t addr) {
    ZoneScopedC(tracy::Color::Orange);
    get_chip(chip)->write_to_device(core, mem_ptr, addr, size_in_bytes);
}

void Cluster::write_to_device_reg(
    const void* mem_ptr, uint32_t size_in_bytes, ChipId chip, CoreCoord core, uint64_t addr) {
    get_chip(chip)->write_to_device_reg(core, mem_ptr, addr, size_in_bytes);
}

void Cluster::dma_write_to_device(const void* src, size_t size, ChipId chip, CoreCoord core, uint64_t addr) {
    ZoneScopedC(tracy::Color::MediumPurple);
    get_chip(chip)->dma_write_to_device(src, size, core, addr);
}

void Cluster::dma_read_from_device(void* dst, size_t size, ChipId chip, CoreCoord core, uint64_t addr) {
    ZoneScopedC(tracy::Color::MediumPurple);
    get_chip(chip)->dma_read_from_device(dst, size, core, addr);
}

void Cluster::dma_multicast_write(
    void* src, size_t size, ChipId chip, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    ZoneScopedC(tracy::Color::MediumPurple);
    get_chip(chip)->dma_multicast_write(src, size, core_start, core_end, addr);
}

void Cluster::read_from_device(void* mem_ptr, ChipId chip, CoreCoord core, uint64_t addr, size_t size) {
    ZoneScopedC(tracy::Color::Orange);
    get_chip(chip)->read_from_device(core, mem_ptr, addr, size);
}

void Cluster::read_from_device_reg(void* mem_ptr, ChipId chip, CoreCoord core, uint64_t addr, uint32_t size) {
    get_chip(chip)->read_from_device_reg(core, mem_ptr, addr, size);
}

void Cluster::noc_multicast_write(
    const void* src, size_t size, ChipId chip, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    ZoneScopedC(tracy::Color::Orange);
    get_chip(chip)->noc_multicast_write(src, size, core_start, core_end, addr);
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

void Cluster::broadcast_tensix_risc_reset_to_cluster(uint32_t reg_value) {
    if (chips_.empty()) {
        // Nowhere to broadcast to.
        return;
    }

    std::set<ChipId> chips_to_exclude = {};
    std::set<uint32_t> rows_to_exclude;
    std::set<uint32_t> columns_to_exclude;
    if (arch_name == tt::ARCH::BLACKHOLE) {
        if (use_translated_coords_for_eth_broadcast) {
            // PCIE and ETH are on these rows in translated space.
            rows_to_exclude = {24, 25};
            // DRAM is on these columns in translated space.
            columns_to_exclude = {17, 18};
        } else {
            rows_to_exclude = {0, 1};
            columns_to_exclude = {0, 8, 9};
        }
    } else if (arch_name == tt::ARCH::WORMHOLE_B0) {
        if (use_translated_coords_for_eth_broadcast) {
            rows_to_exclude = {16, 17};
            columns_to_exclude = {16, 17};
        } else {
            rows_to_exclude = {0, 6};
            columns_to_exclude = {0, 5};
        }
    }
    broadcast_write_to_cluster(
        &reg_value,
        sizeof(uint32_t),
        0xFFB121B0,
        chips_to_exclude,
        rows_to_exclude,
        columns_to_exclude,
        use_translated_coords_for_eth_broadcast);
    // Ensure that reset signal is globally visible.
    wait_for_non_mmio_flush();
}

void Cluster::set_power_state(DevicePowerState device_state) {
    for (auto& [_, chip] : chips_) {
        chip->set_power_state(device_state);
    }
}

void Cluster::deassert_resets_and_set_power_state() {
    ZoneScopedC(tracy::Color::DarkGreen);
    // Assert tensix resets on all chips in cluster.
    assert_risc_reset();

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
    ZoneScopedC(tracy::Color::DarkGreen);
    log_info(LogUMD, "Starting devices in cluster");
    if (device_params.init_device) {
        for (auto chip_id : all_chip_ids_) {
            get_chip(chip_id)->start_device(device_params.dram_membar_subchannel);
        }

        deassert_resets_and_set_power_state();
    }
    log_info(LogUMD, "Starting devices in cluster completed.");
}

void Cluster::close_device() {
    ZoneScopedC(tracy::Color::DarkRed);
    log_info(LogUMD, "Closing devices in cluster");
    // Close remote device first because sending risc reset requires corresponding pcie device to be active.
    for (auto remote_chip_id : remote_chip_ids_) {
        get_chip(remote_chip_id)->close_device();
    }

    for (auto chip_id : local_chip_ids_) {
        get_chip(chip_id)->close_device();
    }
    log_info(LogUMD, "Closing devices in cluster completed.");
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

std::optional<SemVer> Cluster::get_ethernet_firmware_version() const { return eth_fw_version; }

std::optional<FirmwareBundleVersion> Cluster::get_firmware_bundle_version() const { return fw_bundle_version; }

void Cluster::set_barrier_address_params(const BarrierAddressParams& barrier_address_params) {
    for (auto& [_, chip] : chips_) {
        chip->set_barrier_address_params(barrier_address_params);
    }
}

std::unique_ptr<ClusterDescriptor> Cluster::create_cluster_descriptor(
    const std::string& sdesc_path,
    IODeviceType device_type,
    const TopologyDiscoveryOptions& topology_discovery_options) {
    ZoneScopedC(tracy::Color::DarkGreen);
    auto adjusted_topology_options = topology_discovery_options;
    if (adjusted_topology_options.device_init_failure_action != TopologyDiscoveryOptions::Action::THROW) {
        log_warning(LogUMD, "Ignoring device init. failures is not supported in Cluster. Overriding to THROW.");
        adjusted_topology_options.device_init_failure_action = TopologyDiscoveryOptions::Action::THROW;
    }
    return TopologyDiscovery::discover(adjusted_topology_options, device_type, sdesc_path).first;
}

}  // namespace tt::umd
