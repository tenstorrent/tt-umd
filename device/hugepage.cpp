/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/hugepage.h"

#include <fcntl.h>     // for O_RDWR and other constants
#include <sys/stat.h>  // for umask
#include <unistd.h>    // for unlink

#include <regex>

#include "logger.hpp"

static const std::string hugepage_dir = "/dev/hugepages-1G";

namespace tt::umd {

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
        log_fatal("{} - Cannot open {}. errno: {}", __FUNCTION__, nr_hugepages_path, std::strerror(errno));
    }

    return num_hugepages;
}

std::string find_hugepage_dir(std::size_t pagesize) {
    static const std::regex hugetlbfs_mount_re(
        fmt::format("^(nodev|hugetlbfs) ({}) hugetlbfs ([^ ]+) 0 0$", hugepage_dir));
    static const std::regex pagesize_re("(?:^|,)pagesize=([0-9]+)([KMGT])(?:,|$)");

    std::ifstream proc_mounts("/proc/mounts");

    for (std::string line; std::getline(proc_mounts, line);) {
        if (std::smatch mount_match; std::regex_match(line, mount_match, hugetlbfs_mount_re)) {
            std::string options = mount_match[3];
            if (std::smatch pagesize_match; std::regex_search(options, pagesize_match, pagesize_re)) {
                std::size_t mount_page_size = std::stoull(pagesize_match[1]);
                switch (pagesize_match[2].str()[0]) {
                    case 'T':
                        mount_page_size <<= 10;
                    case 'G':
                        mount_page_size <<= 10;
                    case 'M':
                        mount_page_size <<= 10;
                    case 'K':
                        mount_page_size <<= 10;
                }

                if (mount_page_size == pagesize) {
                    return mount_match[2];
                }
            }
        }
    }

    log_warning(
        LogSiliconDriver,
        "ttSiliconDevice::find_hugepage_dir: no huge page mount found in /proc/mounts for path: {} with hugepage_size: "
        "{}.",
        hugepage_dir,
        pagesize);
    return std::string();
}

int open_hugepage_file(const std::string& dir, chip_id_t physical_device_id, uint16_t channel) {
    std::vector<char> filename;
    static const char pipeline_name[] = "tenstorrent";

    filename.insert(filename.end(), dir.begin(), dir.end());
    if (filename.back() != '/') {
        filename.push_back('/');
    }

    // In order to limit number of hugepages while transition from shared hugepage (1 per system) to unique
    // hugepage per device, will share original/shared hugepage filename with physical device 0.
    if (physical_device_id != 0 || channel != 0) {
        std::string device_id_str = fmt::format("device_{}_", physical_device_id);
        filename.insert(filename.end(), device_id_str.begin(), device_id_str.end());
    }

    if (channel != 0) {
        std::string channel_id_str = fmt::format("channel_{}_", channel);
        filename.insert(filename.end(), channel_id_str.begin(), channel_id_str.end());
    }

    filename.insert(filename.end(), std::begin(pipeline_name), std::end(pipeline_name));  // includes NUL terminator

    std::string filename_str(filename.begin(), filename.end());
    filename_str.erase(
        std::find(filename_str.begin(), filename_str.end(), '\0'),
        filename_str.end());  // Erase NULL terminator for printing.
    log_debug(
        LogSiliconDriver,
        "ttSiliconDevice::open_hugepage_file: using filename: {} for physical_device_id: {} channel: {}",
        filename_str.c_str(),
        physical_device_id,
        channel);

    // Save original and set umask to unrestricted.
    auto old_umask = umask(0);

    int fd =
        open(filename.data(), O_RDWR | O_CREAT | O_CLOEXEC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IWOTH | S_IROTH);
    if (fd == -1 && errno == EACCES) {
        log_warning(
            LogSiliconDriver,
            "ttSiliconDevice::open_hugepage_file could not open filename: {} on first try, unlinking it and retrying.",
            filename_str);
        unlink(filename.data());
        fd = open(
            filename.data(), O_RDWR | O_CREAT | O_CLOEXEC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IWOTH | S_IROTH);
    }

    // Restore original mask
    umask(old_umask);

    if (fd == -1) {
        log_warning(LogSiliconDriver, "open_hugepage_file failed");
        return -1;
    }

    return fd;
}

}  // namespace tt::umd
