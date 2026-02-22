// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/multi_arch_topology_discovery.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "fmt/format.h"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

std::unordered_set<tt::ARCH> MultiArchTopologyDiscovery::get_available_architectures() {
    std::unordered_set<tt::ARCH> architectures;

    try {
        auto pci_devices = PCIDevice::enumerate_devices_info();
        for (const auto& [ordinal, info] : pci_devices) {
            tt::ARCH arch = info.get_arch();
            if (arch != tt::ARCH::Invalid) {
                architectures.insert(arch);
            }
        }
    } catch (const std::exception& e) {
        log_warning(LogUMD, "Failed to enumerate PCI devices: {}", e.what());
    }

    return architectures;
}

std::string MultiArchTopologyDiscovery::set_visible_devices_filter(const std::unordered_set<int>& ordinals) {
    // Save current value.
    const char* current = std::getenv("TT_VISIBLE_DEVICES");
    std::string previous_value = current ? current : "";

    // Build new value: comma-separated ordinals.
    std::ostringstream oss;
    bool first = true;
    for (int ordinal : ordinals) {
        if (!first) {
            oss << ",";
        }
        oss << ordinal;
        first = false;
    }

    // Set new value.
    std::string new_value = oss.str();
    if (!new_value.empty()) {
        setenv("TT_VISIBLE_DEVICES", new_value.c_str(), 1);
        log_debug(LogUMD, "Set TT_VISIBLE_DEVICES={} for architecture filtering", new_value);
    }

    return previous_value;
}

void MultiArchTopologyDiscovery::restore_visible_devices(const std::string& previous_value) {
    if (previous_value.empty()) {
        unsetenv("TT_VISIBLE_DEVICES");
        log_debug(LogUMD, "Cleared TT_VISIBLE_DEVICES filter");
    } else {
        setenv("TT_VISIBLE_DEVICES", previous_value.c_str(), 1);
        log_debug(LogUMD, "Restored TT_VISIBLE_DEVICES={}", previous_value);
    }
}

MultiArchTopologyDiscovery::ArchCluster MultiArchTopologyDiscovery::discover_single_architecture(
    tt::ARCH target_arch, const TopologyDiscoveryOptions& options) {
    ArchCluster cluster(target_arch);

    try {
        // Find PCI devices for this architecture.
        auto pci_devices = PCIDevice::enumerate_devices_info();
        std::unordered_set<int> target_ordinals;

        for (const auto& [ordinal, info] : pci_devices) {
            if (info.get_arch() == target_arch) {
                target_ordinals.insert(ordinal);
                cluster.pci_ordinals.insert(ordinal);
            }
        }

        if (target_ordinals.empty()) {
            cluster.error_message =
                fmt::format("No PCI devices found for architecture {}", static_cast<int>(target_arch));
            log_info(LogUMD, "{}", cluster.error_message);
            return cluster;
        }

        log_info(
            LogUMD,
            "Discovering architecture {} with {} PCI device(s)",
            static_cast<int>(target_arch),
            target_ordinals.size());

        // Filter TT_VISIBLE_DEVICES to this architecture.
        std::string previous_visible = set_visible_devices_filter(target_ordinals);

        try {
            // Run topology discovery isolated to this architecture.
            auto [descriptor, devices] = TopologyDiscovery::discover(options);

            cluster.descriptor = std::move(descriptor);
            cluster.devices = std::move(devices);
            cluster.discovery_successful = true;

            log_info(
                LogUMD,
                "Successfully discovered {} device(s) for architecture {}",
                cluster.devices.size(),
                static_cast<int>(target_arch));

        } catch (const std::exception& e) {
            cluster.error_message = fmt::format(
                "TopologyDiscovery failed for architecture {}: {}", static_cast<int>(target_arch), e.what());
            log_warning(LogUMD, "{}", cluster.error_message);
        }

        // Restore environment.
        restore_visible_devices(previous_visible);

    } catch (const std::exception& e) {
        cluster.error_message =
            fmt::format("PCI enumeration failed for architecture {}: {}", static_cast<int>(target_arch), e.what());
        log_error(LogUMD, "{}", cluster.error_message);
    }

    return cluster;
}

std::map<tt::ARCH, MultiArchTopologyDiscovery::ArchCluster> MultiArchTopologyDiscovery::discover_by_architecture(
    const TopologyDiscoveryOptions& base_options) {
    std::map<tt::ARCH, ArchCluster> clusters;

    log_info(LogUMD, "Starting multi-architecture topology discovery");

    // Detect available architectures.
    auto architectures = get_available_architectures();

    if (architectures.empty()) {
        log_warning(LogUMD, "No Tenstorrent devices found on PCI bus");
        return clusters;
    }

    log_info(LogUMD, "Detected {} architecture(s) on PCI bus", architectures.size());

    // Discover each architecture independently.
    for (tt::ARCH arch : architectures) {
        log_info(LogUMD, "Discovering devices for architecture {}...", static_cast<int>(arch));

        auto cluster = discover_single_architecture(arch, base_options);

        if (cluster.discovery_successful) {
            log_info(
                LogUMD,
                "  ✓ Architecture {} discovery successful: {} device(s) found",
                static_cast<int>(arch),
                cluster.devices.size());
            clusters[arch] = std::move(cluster);
        } else {
            log_warning(
                LogUMD, "  ✗ Architecture {} discovery failed: {}", static_cast<int>(arch), cluster.error_message);
            // Still add to map to preserve error information.
            clusters[arch] = std::move(cluster);
        }
    }

    // Summary.
    size_t total_devices = 0;
    size_t successful_archs = 0;
    for (const auto& [arch, cluster] : clusters) {
        if (cluster.discovery_successful) {
            total_devices += cluster.devices.size();
            successful_archs++;
        }
    }

    log_info(
        LogUMD,
        "Multi-architecture discovery complete: {} architecture(s) successful, {} total device(s)",
        successful_archs,
        total_devices);

    return clusters;
}

}  // namespace tt::umd
