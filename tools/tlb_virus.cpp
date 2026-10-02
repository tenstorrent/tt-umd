// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cstddef>
#include <cstdint>
#include <cxxopts.hpp>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "common.hpp"
#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    cxxopts::Options options("tlb_virus", "Allocate TLBs in an infinite loop until failure for all sizes.");

    options.add_options()("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        // Map to track allocations per device and per size: device_id -> (size -> (allocated, total)).
        std::map<int, std::map<size_t, std::pair<int, uint32_t>>> tlb_allocation_summary;

        for (int pci_device_id : PCIDevice::enumerate_devices()) {
            auto tt_device = TTDevice::create(pci_device_id);
            std::vector<std::unique_ptr<TlbHandle>> allocated_tlbs;
            tt_device->init_tt_device();
            tt::ARCH arch = tt_device->get_arch();
            auto pci_device = tt_device->get_pci_device();
            const std::vector<TlbSizeClass>& tlb_size_classes = get_architecture_tlbs(arch).size_classes;

            log_info(
                tt::LogUMD,
                "Starting TLB stress test on device {} (architecture: {})",
                pci_device_id,
                tt::arch_to_str(arch));

            // Initialize tracking for this device, per window size.
            for (const TlbSizeClass& size_class : tlb_size_classes) {
                tlb_allocation_summary[pci_device_id][size_class.size] = {0, size_class.count};
            }

            for (const TlbSizeClass& size_class : tlb_size_classes) {
                const size_t tlb_size = size_class.size;
                int total_allocated = 0;
                log_info(tt::LogUMD, "Testing TLB size: {} bytes", tlb_size);

                while (true) {
                    try {
                        auto tlb_handle = pci_device->allocate_tlb(tlb_size, TlbMapping::WC);
                        // One line per allocated TLB, and the loop deliberately allocates until it
                        // fails; the per-size summary below reports the counts that matter.
                        log_debug(
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

        log_info(tt::LogUMD, "TLB stress test completed.");

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
