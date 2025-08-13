/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/hugepage.h"

#include <fcntl.h>     // for O_RDWR and other constants
#include <sys/stat.h>  // for umask

#include <fstream>
#include <regex>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "cpuset_lib.hpp"

namespace tt::umd {

const uint32_t g_MAX_HOST_MEM_CHANNELS = 4;

uint32_t get_num_hugepages() {
    std::string nr_hugepages_path = "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages";
    std::ifstream hugepages_file(nr_hugepages_path);
    uint32_t num_hugepages = 0;

    if (hugepages_file.is_open()) {
        std::string value;
        std::getline(hugepages_file, value);
        num_hugepages = std::stoi(value);
        log_debug(LogSiliconDriver, "Parsed num_hugepages: {} from {}", num_hugepages, nr_hugepages_path);
    } else {
        TT_THROW(fmt::format("{} - Cannot open {}. errno: {}", __FUNCTION__, nr_hugepages_path, std::strerror(errno)));
    }

    return num_hugepages;
}

uint32_t get_available_num_host_mem_channels(
    const uint32_t num_channels_per_device_target, const uint16_t device_id, const uint16_t revision_id) {
    // To minimally support hybrid dev systems with mix of ARCH, get only devices matching current ARCH's device_id.
    uint32_t total_num_tt_mmio_devices = tt::cpuset::tt_cpuset_allocator::get_num_tt_pci_devices();
    uint32_t num_tt_mmio_devices_for_arch =
        tt::cpuset::tt_cpuset_allocator::get_num_tt_pci_devices_by_pci_device_id(device_id, revision_id);
    uint32_t total_hugepages = get_num_hugepages();

    // This shouldn't happen on silicon machines.
    if (num_tt_mmio_devices_for_arch == 0) {
        log_warning(
            LogSiliconDriver,
            "No TT devices found that match PCI device_id: 0x{:x} revision: {}, returning NumHostMemChannels:0",
            device_id,
            revision_id);
        return 0;
    }

    // GS will use P2P + 1 channel, others may support 4 host channels. Apply min of 1 to not completely break setups
    // that were incomplete ie fewer hugepages than devices, which would partially work previously for some devices.
    uint32_t num_channels_per_device_available =
        std::min(num_channels_per_device_target, std::max((uint32_t)1, total_hugepages / num_tt_mmio_devices_for_arch));

    // Perform some helpful assertion checks to guard against common pitfalls that would show up as runtime issues later
    // on.
    if (total_num_tt_mmio_devices > num_tt_mmio_devices_for_arch) {
        log_warning(
            LogSiliconDriver,
            "Hybrid system mixing different TTDevices - this is not well supported. Ensure sufficient "
            "Hugepages/HostMemChannels per device.");
    }

    if (total_hugepages < num_tt_mmio_devices_for_arch) {
        log_warning(
            LogSiliconDriver,
            "Insufficient NumHugepages: {} should be at least NumMMIODevices: {} for device_id: 0x{:x} revision: {}. "
            "NumHostMemChannels would be 0, bumping to 1.",
            total_hugepages,
            num_tt_mmio_devices_for_arch,
            device_id,
            revision_id);
    }

    if (num_channels_per_device_available < num_channels_per_device_target) {
        log_warning(
            LogSiliconDriver,
            "NumHostMemChannels: {} used for device_id: 0x{:x} less than target: {}. Workload will fail if it exceeds "
            "NumHostMemChannels. Increase Number of Hugepages.",
            num_channels_per_device_available,
            device_id,
            num_channels_per_device_target);
    }

    TT_ASSERT(
        num_channels_per_device_available <= g_MAX_HOST_MEM_CHANNELS,
        "NumHostMemChannels: {} exceeds supported maximum: {}, this is unexpected.",
        num_channels_per_device_available,
        g_MAX_HOST_MEM_CHANNELS);

    return num_channels_per_device_available;
}

}  // namespace tt::umd
