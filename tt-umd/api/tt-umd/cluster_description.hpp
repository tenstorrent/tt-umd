// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "tt-umd/topology/topology_discovery_error.hpp"
#include "tt-umd/tt_device/tt_device_error.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/utils/semver.hpp"

namespace tt::umd {

// Why a chip found during topology discovery could not be used.
using DeviceHealthError = std::variant<
    error::FirmwareStartupError,
    error::NocHangError,
    error::PcieHangError,
    error::UnsupportedCMFWError,
    error::CMFWMismatchError,
    error::UnexpectedRoutingFirmwareConfigError,
    error::EthFirmwareMismatchError,
    error::EthFirmwareHeartbeatError>;

// Everything known about one chip. Only arch is required; every other field defaults to "not known".
struct ChipDescription {
    tt::ARCH arch = tt::ARCH::Invalid;
    BoardType board_type = BoardType::UNKNOWN;
    std::optional<uint64_t> board_id;
    // Identifies the physical chip across processes. A cluster where any chip lacks one gets placeholder ids that
    // are stable only within the process.
    std::optional<uint64_t> unique_id;
    // Position reported by the routing firmware; absent on systems that don't use Ethernet coordinates.
    std::optional<EthCoord> eth_coord;
    HarvestingMasks harvesting_masks;
    bool noc_translation_enabled = false;
    // Set only for chips the host reaches directly (MMIO capable): the host's id for the device, e.g. N in
    // /dev/tenstorrent/N.
    std::optional<ChipId> mmio_device_id;
    // The MMIO chip a remote chip is reached through. When absent it's looked up among the chip's board mates.
    std::optional<ChipId> closest_mmio_chip;
    // PCIe link details, valid only on MMIO chips.
    std::optional<std::string> pci_bdf;
    std::optional<uint32_t> pcie_lane_count;
    // Locate the chip in its system, e.g. on a UBB tray.
    std::optional<uint16_t> pci_bus_id;
    std::optional<uint8_t> asic_location;
    std::set<uint32_t> active_eth_channels;
    std::set<uint32_t> idle_eth_channels;
};

// An Ethernet link between two chips of the cluster. Each link is listed once; it is reported in both directions.
struct EthLink {
    ChipId chip_a;
    EthernetChannel channel_a;
    ChipId chip_b;
    EthernetChannel channel_b;
};

// An Ethernet link from a chip of the cluster to a chip outside it, named by its unique id.
struct RemoteEthLink {
    ChipId chip;
    EthernetChannel channel;
    uint64_t remote_unique_id;
    EthernetChannel remote_channel;
};

// Plain description of a cluster, from which create_cluster_descriptor builds a ClusterDescriptor.
struct ClusterDescription {
    std::map<ChipId, ChipDescription> chips;
    std::vector<EthLink> eth_links;
    std::vector<RemoteEthLink> remote_eth_links;
    // Chips found but left out of chips because they could not be used. Links may still name them.
    std::vector<ChipId> unhealthy_chips;
    std::map<ChipId, std::vector<DeviceHealthError>> health_errors;
    std::optional<std::string> cluster_id;
    IODeviceType io_device_type = IODeviceType::PCIe;
    std::optional<SemVer> eth_fw_version;
    std::optional<FirmwareBundleVersion> fw_bundle_version;
};

}  // namespace tt::umd
