// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "umd/device/chip/chip.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace YAML {
class Node;
}

namespace tt::umd {
class Cluster;

class ClusterDescriptor {
    // TODO: Only Topo Discovery should have access.
    friend class Cluster;
    friend class TopologyDiscovery;

public:
    /* Construction related functions. */
    ClusterDescriptor() = default;

    /**
     * Serializes the cluster descriptor to a YAML string.
     */
    std::string serialize() const;

    /**
     * Serializes the cluster descriptor to a YAML file.
     * @param dest_file Path to the file where the descriptor will be serialized. If empty filename is passed, the
     * default randomly generated path will be used.
     */
    std::filesystem::path serialize_to_file(const std::filesystem::path &dest_file = "") const;

    /**
     * Creates a cluster descriptor from a YAML file.
     * @param cluster_descriptor_file_path Path to the YAML file containing the cluster descriptor.
     */
    static std::unique_ptr<ClusterDescriptor> create_from_yaml(const std::string &cluster_descriptor_file_path);

    /**
     * Creates a cluster descriptor from a YAML file content.
     * @param cluster_descriptor_file_content Content of the YAML file containing the cluster descriptor.
     */
    static std::unique_ptr<ClusterDescriptor> create_from_yaml_content(
        const std::string &cluster_descriptor_file_content);

    /**
     * Creates a mock cluster descriptor with the given logical device IDs and architecture.
     * This function is used to create mock cluster descriptor yaml files, for example for simulation.
     * @param logical_device_ids Vector of logical device IDs to be included in the mock cluster.
     * @param arch Architecture of the mock cluster.
     */
    static std::unique_ptr<ClusterDescriptor> create_mock_cluster(
        const std::unordered_set<ChipId> &logical_device_ids, tt::ARCH arch, bool noc_translation_enabled);

    /**
     * Creates a constrained cluster descriptor that only contains the chips specified in target_chip_ids.
     * @param full_cluster_desc Pointer to the full cluster descriptor from which the constrained descriptor will be
     * created.
     * @param target_chip_ids Set of logical chip IDs for filtering.
     */
    static std::unique_ptr<ClusterDescriptor> create_constrained_cluster_descriptor(
        const ClusterDescriptor *full_cluster_desc, const std::unordered_set<ChipId> &target_chip_ids);

    /* Getters for various chip related information. */

    /**
     * Return whether a chip is connected through a PCIe link.
     *
     * @param chip_id Logical chip id to check.
     */
    bool is_chip_mmio_capable(const ChipId chip_id) const;

    /**
     * Opposite of is_chip_mmio_capable.
     *
     * @param chip_id Logical chip id to check.
     */
    bool is_chip_remote(const ChipId chip_id) const;

    /**
     * Returns the number of chips in the cluster descriptor.
     */
    std::size_t get_number_of_chips() const;

    /**
     * Returns a set of logical chip IDs for all chips in the cluster descriptor.
     */
    const std::unordered_set<ChipId> &get_all_chips() const;

    /**
     * Function to help with sorting the passed set into a vector such that local chips are first, followed by remote
     * chips.
     */
    const std::vector<ChipId> get_chips_local_first(const std::unordered_set<ChipId> &chips) const;

    /**
     * Returns the architecture of the cluster. Throws an exception if the architecture is Invalid or there are no
     * chips.
     */
    tt::ARCH get_arch() const;

    /**
     * Returns the architecture of a specific chip.
     * @param chip_id Logical chip ID to get the architecture for.
     */
    tt::ARCH get_arch(ChipId chip_id) const;

    /**
     * Returns the board type of a specific chip.
     * @param chip_id Logical chip ID to get the board type for.
     */
    BoardType get_board_type(ChipId chip_id) const;

    /**
     * Returns a set of chips present on a specific board.
     * @param board_id Board ID to use for checking the chips.
     */
    std::unordered_set<ChipId> get_board_chips(const uint64_t board_id) const;

    /**
     * Returns board ID for a chip.
     * @param chip Logical chip ID to get the board ID for.
     */
    uint64_t get_board_id_for_chip(const ChipId chip) const;

    /**
     * Returns the map of logical chip IDs and information on whether NOC translation table is enabled for that chip.
     */
    const std::unordered_map<ChipId, bool> &get_noc_translation_table_en() const;

    /**
     * Returns the map of logical chip IDs and their ETH coordinates as reported by the routing firmware.
     */
    const std::unordered_map<ChipId, EthCoord> &get_chip_locations() const;

    /**
     * Return ETH coordinates as reported by the routing firmware for given logical chip ID.
     */
    const EthCoord get_chip_location(const ChipId chip) const;

    /**
     * Returns the map of logical chip IDs and their ETH locations as reported by the routing firmware.
     */
    const std::unordered_map<ChipId, uint64_t> &get_chip_unique_ids() const;

    /**
     * Returns the map of logical chip IDs and their PCIe ids as reported by the operating system.
     */
    const std::unordered_map<ChipId, ChipId> &get_chips_with_mmio() const;

    /**
     * Returns the harvesting masks for a given chip ID.
     * @param chip_id Logical chip ID to get the harvesting masks for.
     */
    HarvestingMasks get_harvesting_masks(ChipId chip_id) const;

    /* Connection related functions. */
    /**
     * Returns the closest PCIe connected chip. If passed chip is a PCIe chip, it will return itself.
     * @param chip Logical chip id to check, can be a PCIe or remote chip.
     */
    ChipId get_closest_mmio_capable_chip(const ChipId chip);

    /*
     * Returns the pairs of channels that are connected where the first entry in the pair corresponds to the argument
     * ordering when calling the function An empty result implies that the two chips do not share any direct connection.
     * @param first Logical chip id of the first chip.
     * @param second Logical chip id of the second chip.
     */
    std::vector<std::tuple<EthernetChannel, EthernetChannel>> get_directly_connected_ethernet_channels_between_chips(
        const ChipId &first, const ChipId &second) const;

    /**
     * Returns a map representing all ethernet connections within the cluster.
     * The map returned maps each chip and its core to a pair representing the remote chip's logical id and its core.
     * All connections are bidirectional (each connection is reported twice).
     */
    const std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<ChipId, EthernetChannel>>> &
    get_ethernet_connections() const;

    /**
     * Returns a map representing all ethernet connections going outside of the cluster.
     * The map returned maps each chip and its core to a pair representing the remote chip's unique id and its core.
     * All connections are bidirectional (each connection is reported twice).
     *
     * Note that in previous function the logical chip id is returned, but here we return unique chip id so it can be
     * matched with another cluster descriptor's information.
     */
    const std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<uint64_t, EthernetChannel>>> &
    get_ethernet_connections_to_remote_devices() const;
    const std::unordered_map<ChipId, std::unordered_set<ChipId>> &get_chips_grouped_by_closest_mmio() const;

    /**
     * Returns wether the ethernet core has an active ethernet link.
     */
    bool ethernet_core_has_active_ethernet_link(ChipId local_chip, EthernetChannel local_ethernet_channel) const;
    std::tuple<ChipId, EthernetChannel> get_chip_and_channel_of_remote_ethernet_core(
        ChipId local_chip, EthernetChannel local_ethernet_channel) const;

    /**
     * Returns the set of active ethernet channels for a given chip.
     * @param chip_id Logical chip ID to check for active ethernet channels.
     */
    std::set<uint32_t> get_active_eth_channels(ChipId chip_id);

    /**
     * Returns the set of idle ethernet channels for a given chip.
     * Idle channels are those that are not currently used by any active ethernet link.
     * @param chip_id Logical chip ID to check for idle ethernet channels.
     */
    std::set<uint32_t> get_idle_eth_channels(ChipId chip_id);

    /**
     * Galaxy specific function.
     */
    ChipId get_shelf_local_physical_chip_coords(ChipId virtual_coord);

    uint8_t get_asic_location(ChipId chip_id) const;

    IODeviceType get_io_device_type() const;

    uint16_t get_bus_id(ChipId chip_id) const;

    const std::unordered_map<ChipId, uint16_t> &get_chip_to_bus_id() const;

    const std::unordered_map<ChipId, std::string> &get_chip_pci_bdfs() const;

private:
    int get_ethernet_link_coord_distance(const EthCoord &location_a, const EthCoord &location_b) const;

    // Helpers during construction of cluster descriptor.
    void add_chip_to_board(ChipId chip_id, uint64_t board_id);

    // Helper functions for filling up the cluster descriptor.
    void load_ethernet_connections_from_connectivity_descriptor(YAML::Node &yaml);
    void fill_galaxy_connections();
    void load_chips_from_connectivity_descriptor(YAML::Node &yaml);
    void merge_cluster_ids();
    void load_harvesting_information(YAML::Node &yaml);
    void fill_chips_grouped_by_closest_mmio();

    // Centralize mock/simulator-only default values that are not coming from YAML.
    void fill_mock_hardcoded_data(ChipId logical_id);

    // Verify for some common mistakes.
    bool verify_cluster_descriptor_info();

    // Return the default randomly generated path for serializing cluster descriptors.
    std::filesystem::path get_default_cluster_descriptor_file_path() const;

    bool verify_board_info_for_chips();

    bool verify_same_architecture();

    bool verify_harvesting_information();

    std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<ChipId, EthernetChannel>>>
        ethernet_connections;
    // TODO: unify uint64_t with ChipUID.
    std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<uint64_t, EthernetChannel>>>
        ethernet_connections_to_remote_devices;
    std::unordered_map<ChipId, EthCoord> chip_locations;
    // reverse map: rack/shelf/y/x -> chip_id
    std::map<int, std::map<int, std::map<int, std::map<int, ChipId>>>> coords_to_chip_ids;
    std::unordered_map<ChipId, ChipId> chips_with_mmio;
    std::unordered_set<ChipId> all_chips;
    std::unordered_map<ChipId, bool> noc_translation_enabled = {};
    std::unordered_map<ChipId, ChipId> closest_mmio_chip_cache = {};
    std::unordered_map<ChipId, BoardType> chip_board_type = {};
    std::unordered_map<ChipId, std::unordered_set<ChipId>> chips_grouped_by_closest_mmio;
    std::unordered_map<ChipId, tt::ARCH> chip_arch = {};
    std::unordered_map<ChipId, uint64_t> chip_unique_ids = {};
    std::map<ChipId, std::set<uint32_t>> active_eth_channels = {};
    std::map<ChipId, std::set<uint32_t>> idle_eth_channels = {};
    std::map<uint64_t, std::unordered_set<ChipId>> board_to_chips = {};
    std::map<ChipId, uint8_t> asic_locations = {};
    std::unordered_map<ChipId, uint64_t> chip_to_board_id = {};
    std::unordered_map<ChipId, std::string> chip_pci_bdfs = {};

    // one-to-many chip connections
    struct Chip2ChipConnection {
        EthCoord source_chip_coord;
        std::unordered_set<EthCoord> destination_chip_coords;
    };

    // shelf_id -> y dim -> list of chip2chip connections between different shelves
    // assumption is that on every row of the shelf there is a chip that is connected to the other shelf
    // there could be one-to-many connections between shelves, i.e. one chip is connected to multiple chips on the other
    // shelf (in case of nebula->galaxy)
    std::unordered_map<int, std::unordered_map<int, Chip2ChipConnection>> galaxy_shelves_exit_chip_coords_per_y_dim =
        {};
    // rack_id -> x dim -> list of chip2chip connections between different racks
    // assumption is that on every row of the rack there is a chip that is connected to the other rack
    std::unordered_map<int, std::unordered_map<int, Chip2ChipConnection>> galaxy_racks_exit_chip_coords_per_x_dim = {};

    std::map<ChipId, HarvestingMasks> harvesting_masks_map = {};

    IODeviceType io_device_type = IODeviceType::PCIe;

    // Bus ID needs to be cached in cluster descriptor for use to pin chip location for UBB trays.
    std::unordered_map<ChipId, uint16_t> chip_to_bus_id = {};

    std::optional<semver_t> fw_bundle_version;

    // Will have value only if there are ETH cores on chips in the cluster.
    std::optional<semver_t> eth_fw_version;
};
}  // namespace tt::umd
