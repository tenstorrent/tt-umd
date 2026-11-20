// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "hugepage.hpp"

#include <fmt/format.h>
#include <mntent.h>
#include <sys/vfs.h>  // for statfs

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "cpuset_lib.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

uint32_t get_num_hugepages() {
    std::string nr_hugepages_path = "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages";
    std::ifstream hugepages_file(nr_hugepages_path);
    uint32_t num_hugepages = 0;

    if (hugepages_file.is_open()) {
        std::string value;
        std::getline(hugepages_file, value);
        num_hugepages = std::stoi(value);
        log_debug(LogUMD, "Parsed num_hugepages: {} from {}", num_hugepages, nr_hugepages_path);
    } else {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("{} - Cannot open {}. errno: {}", __FUNCTION__, nr_hugepages_path, std::strerror(errno)));
    }

    return num_hugepages;
}

uint32_t get_available_num_host_mem_channels(
    const uint32_t num_channels_per_device_target, const uint16_t device_id, const uint8_t revision_id) {
    // To minimally support hybrid dev systems with mix of ARCH, get only devices matching current ARCH's device_id.
    size_t total_num_tt_mmio_devices = tt::cpuset::cpuset_allocator::get_num_tt_pci_devices();
    uint32_t num_tt_mmio_devices_for_arch =
        tt::cpuset::cpuset_allocator::get_num_tt_pci_devices_by_pci_device_id(device_id, revision_id);
    uint32_t total_hugepages = get_num_hugepages();

    // This shouldn't happen on silicon machines.
    if (num_tt_mmio_devices_for_arch == 0) {
        log_warning(
            LogUMD,
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
            LogUMD,
            "Hybrid system mixing different TTDevices - this is not well supported. Ensure sufficient "
            "Hugepages/HostMemChannels per device.");
    }

    if (total_hugepages < num_tt_mmio_devices_for_arch) {
        log_warning(
            LogUMD,
            "Insufficient NumHugepages: {} should be at least NumMMIODevices: {} for device_id: 0x{:x} revision: {}. "
            "NumHostMemChannels would be 0, bumping to 1.",
            total_hugepages,
            num_tt_mmio_devices_for_arch,
            device_id,
            revision_id);
    }

    if (num_channels_per_device_available < num_channels_per_device_target) {
        log_warning(
            LogUMD,
            "NumHostMemChannels: {} used for device_id: 0x{:x} less than target: {}. Workload will fail if it exceeds "
            "NumHostMemChannels. Increase Number of Hugepages.",
            num_channels_per_device_available,
            device_id,
            num_channels_per_device_target);
    }

    UMD_ASSERT(
        num_channels_per_device_available <= MAX_HOST_MEM_CHANNELS,
        error::RuntimeError,
        fmt::format(
            "NumHostMemChannels: {} exceeds supported maximum: {}, this is unexpected.",
            num_channels_per_device_available,
            MAX_HOST_MEM_CHANNELS));

    return num_channels_per_device_available;
}

namespace {

constexpr std::string_view hugepage_sysfs_dir = "/sys/kernel/mm/hugepages/hugepages-1048576kB";

std::optional<uint64_t> read_hugepage_counter(std::string_view counter) {
    std::ifstream file(fmt::format("{}/{}", hugepage_sysfs_dir, counter));
    uint64_t value = 0;
    if (file >> value) {
        return value;
    }
    return std::nullopt;
}

// Files on a hugetlbfs mount keep their hugepages out of the pool until they are removed, even after every process
// that mapped them has exited. Returns the 1GB mounts that hold any, with the number of non-empty files on each.
std::vector<std::pair<std::string, size_t>> find_files_on_1g_hugetlbfs_mounts() {
    std::vector<std::pair<std::string, size_t>> mounts_with_files;
    FILE* mounts = setmntent("/proc/mounts", "r");
    if (mounts == nullptr) {
        return mounts_with_files;
    }

    mntent entry{};
    std::array<char, 4096> entry_strings{};
    while (getmntent_r(mounts, &entry, entry_strings.data(), entry_strings.size()) != nullptr) {
        if (std::string_view(entry.mnt_type) != "hugetlbfs") {
            continue;
        }

        // hugetlbfs reports its page size as the block size, while /proc/mounts omits it for the default page size.
        struct statfs mount_stats = {};
        if (statfs(entry.mnt_dir, &mount_stats) != 0 ||
            static_cast<uint64_t>(mount_stats.f_bsize) != HUGEPAGE_REGION_SIZE) {
            continue;
        }

        size_t num_files = 0;
        std::error_code iteration_error;
        for (std::filesystem::directory_iterator file(entry.mnt_dir, iteration_error), end;
             !iteration_error && file != end;
             file.increment(iteration_error)) {
            std::error_code stat_error;
            if (file->is_regular_file(stat_error) && file->file_size(stat_error) > 0 && !stat_error) {
                num_files++;
            }
        }
        if (num_files > 0) {
            mounts_with_files.emplace_back(entry.mnt_dir, num_files);
        }
    }
    endmntent(mounts);
    return mounts_with_files;
}

}  // namespace

std::string describe_hugepage_pool() {
    const std::optional<uint64_t> free_hugepages = read_hugepage_counter("free_hugepages");
    const std::optional<uint64_t> reserved_hugepages = read_hugepage_counter("resv_hugepages");
    const std::optional<uint64_t> total_hugepages = read_hugepage_counter("nr_hugepages");
    std::string description;
    if (free_hugepages && reserved_hugepages && total_hugepages) {
        // Free pages include those other mappings reserved but have not touched yet; a new mapping cannot take them.
        const uint64_t available_hugepages =
            *free_hugepages > *reserved_hugepages ? *free_hugepages - *reserved_hugepages : 0;
        description = fmt::format(
            "{} of {} 1GB hugepages are available ({} free, of which {} are reserved by other mappings).",
            available_hugepages,
            *total_hugepages,
            *free_hugepages,
            *reserved_hugepages);
    } else {
        description = fmt::format("The 1GB hugepage pool could not be read from {}.", hugepage_sysfs_dir);
    }

    for (const auto& [mount, num_files] : find_files_on_1g_hugetlbfs_mounts()) {
        description += fmt::format(
            " {} files in {} hold 1GB hugepages (typically left there by older UMD versions); remove them if no "
            "running process uses them.",
            num_files,
            mount);
    }
    return description;
}

}  // namespace tt::umd
