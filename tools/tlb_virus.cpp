// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cxxopts.hpp>
#include <iostream>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "common.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    cxxopts::Options options("tlb_virus", "Allocate TLBs in an infinite loop until failure for all sizes.");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        std::vector<std::unique_ptr<TlbHandle>> allocated_tlbs;
        for (int pci_device_id : PCIDevice::enumerate_devices()) {
            auto tt_device = TTDevice::create(pci_device_id);
            tt_device->init_tt_device();
            tt::ARCH arch = tt_device->get_arch();
            auto pci_device = tt_device->get_pci_device();

            const std::vector<size_t>& tlb_sizes = tt_device->get_architecture_implementation()->get_tlb_sizes();

            log_info(
                tt::LogUMD,
                "Starting TLB stress test on device {} (architecture: {})",
                pci_device_id,
                tt::arch_to_str(arch));

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

    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error: {}", e.what());
        return 1;
    }

    return 0;
}
