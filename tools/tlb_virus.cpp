// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cxxopts.hpp>
#include <iostream>
#include <map>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "common.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"

using namespace tt::umd;

// Helper function to get TLB count for a given size from architecture.
uint32_t get_tlb_count_for_size(architecture_implementation* arch_impl, size_t tlb_size) {
    static constexpr size_t one_mb = 1 << 20;
    static constexpr size_t one_gb = 1 << 30;

    if (tlb_size == 1 * one_mb) {
        return arch_impl->get_tlb_1m_base_and_count().second;
    } else if (tlb_size == 2 * one_mb) {
        return arch_impl->get_tlb_2m_base_and_count().second;
    } else if (tlb_size == 16 * one_mb) {
        return arch_impl->get_tlb_16m_base_and_count().second;
    } else if (tlb_size == 4ULL * one_gb) {
        return arch_impl->get_tlb_4g_base_and_count().second;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("tlb_virus", "Allocate TLBs in an infinite loop until failure for all sizes.");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        std::vector<std::unique_ptr<TlbHandle>> allocated_tlbs;
        // Map to track allocations per device and per size: device_id -> (size -> (allocated, total)).
        std::map<int, std::map<size_t, std::pair<int, uint32_t>>> tlb_allocation_summary;

        for (int pci_device_id : PCIDevice::enumerate_devices()) {
            auto tt_device = TTDevice::create(pci_device_id);
            tt_device->init_tt_device();
            tt::ARCH arch = tt_device->get_arch();
            auto pci_device = tt_device->get_pci_device();
            auto arch_impl = tt_device->get_architecture_implementation();

            const std::vector<size_t>& tlb_sizes = arch_impl->get_tlb_sizes();

            log_info(
                tt::LogUMD,
                "Starting TLB stress test on device {} (architecture: {})",
                pci_device_id,
                tt::arch_to_str(arch));

            // Fetch and log TLB counts per size for this architecture.
            for (size_t tlb_size : tlb_sizes) {
                uint32_t total_count = get_tlb_count_for_size(arch_impl, tlb_size);
                // Initialize tracking for this device and size.
                tlb_allocation_summary[pci_device_id][tlb_size] = {0, total_count};
            }

            for (size_t tlb_size : tlb_sizes) {
                int total_allocated = 0;
                log_info(tt::LogUMD, "Testing TLB size: {} bytes", tlb_size);

                while (true) {
                    try {
                        auto tlb_handle = pci_device->allocate_tlb(tlb_size, TlbMapping::WC);
                        log_info(
                            tt::LogUMD, "Allocated TLB id: {} of size {} bytes", tlb_handle->get_tlb_id(), tlb_size);
                        allocated_tlbs.emplace_back(std::move(tlb_handle));
                        total_allocated++;
                        // Update allocation count for this device and size.
                        tlb_allocation_summary[pci_device_id][tlb_size].first = total_allocated;
                    } catch (const std::exception& e) {
                        log_info(
                            tt::LogUMD,
                            "Failed to allocate TLB of size {} bytes after {} successful allocations of this size. "
                            "Error: {}",
                            tlb_size,
                            total_allocated,
                            e.what());
                        break;
                    }
                }
            }
        }

        // TLBs will be automatically freed when allocated_tlbs goes out of scope.
        log_info(tt::LogUMD, "TLB stress test completed. All TLBs will be freed on exit.");

        // Print summary for all devices.
        log_info(tt::LogUMD, "=== TLB Allocation Summary ===");
        for (const auto& [device_id, size_map] : tlb_allocation_summary) {
            log_info(tt::LogUMD, "Device {}:", device_id);
            for (const auto& [size, counts] : size_map) {
                log_info(
                    tt::LogUMD,
                    "  Size {} bytes: {} of {} TLBs were successfully allocated",
                    size,
                    counts.first,
                    counts.second);
            }
        }

    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error: {}", e.what());
        return 1;
    }

    return 0;
}
