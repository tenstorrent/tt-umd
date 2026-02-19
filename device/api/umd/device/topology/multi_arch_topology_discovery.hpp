// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>
#include <unordered_set>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

/**
 * @brief Multi-architecture topology discovery for heterogeneous systems.
 *
 * Enables discovery of devices across multiple architectures (e.g., Wormhole + Blackhole)
 * by creating separate, isolated clusters per architecture. Each cluster is independently
 * validated and managed, avoiding conflicts from mixed-architecture validation.
 *
 * This is designed for monitoring tools, heterogeneous compute nodes, and systems where
 * different architectures coexist but workloads target specific architectures at runtime.
 */
class MultiArchTopologyDiscovery {
public:
    /**
     * @brief Represents a cluster of devices for a single architecture.
     */
    struct ArchCluster {
        tt::ARCH arch;
        std::unique_ptr<ClusterDescriptor> descriptor;
        std::map<uint64_t, std::unique_ptr<TTDevice>> devices;
        std::unordered_set<int> pci_ordinals;  // PCI device ordinals in this cluster
        bool discovery_successful;
        std::string error_message;

        ArchCluster() : arch(tt::ARCH::Invalid), discovery_successful(false) {}

        ArchCluster(tt::ARCH a) : arch(a), discovery_successful(false) {}
    };

    /**
     * @brief Discover devices grouped by architecture, creating isolated clusters.
     *
     * @param options Base topology discovery options (applied to each architecture)
     * @return Map of architecture to its cluster (only successful discoveries included)
     *
     * Algorithm:
     * 1. Enumerate all PCI devices and group by architecture
     * 2. For each architecture:
     *    a. Filter environment (TT_VISIBLE_DEVICES) to only that architecture's devices
     *    b. Run TopologyDiscovery::discover() in isolated context
     *    c. Store result in separate ArchCluster
     * 3. Return all successful clusters
     *
     * Benefits:
     * - Each architecture validates independently (no mixed-arch conflicts)
     * - Remote devices discovered per-architecture (n300 R chip with n300 L)
     * - Telemetry works during execution (uses TTDevice per-arch)
     * - Failures in one architecture don't affect others
     */
    static std::map<tt::ARCH, ArchCluster> discover_by_architecture(
        const TopologyDiscoveryOptions& base_options = TopologyDiscoveryOptions());

    /**
     * @brief Discover devices for a specific architecture only.
     *
     * @param target_arch Architecture to discover
     * @param options Topology discovery options
     * @return ArchCluster for the specified architecture (check discovery_successful)
     */
    static ArchCluster discover_single_architecture(
        tt::ARCH target_arch, const TopologyDiscoveryOptions& options = TopologyDiscoveryOptions());

    /**
     * @brief Get list of architectures present in the system (via PCI enumeration).
     *
     * @return Set of architectures detected on PCI bus
     */
    static std::unordered_set<tt::ARCH> get_available_architectures();

private:
    /**
     * @brief Helper to set TT_VISIBLE_DEVICES to filter to specific PCI ordinals.
     *
     * @param ordinals PCI device ordinals to make visible
     * @return Previous value of TT_VISIBLE_DEVICES (empty string if unset)
     */
    static std::string set_visible_devices_filter(const std::unordered_set<int>& ordinals);

    /**
     * @brief Restore TT_VISIBLE_DEVICES to previous value.
     *
     * @param previous_value Value to restore (empty to unset)
     */
    static void restore_visible_devices(const std::string& previous_value);
};

}  // namespace tt::umd
