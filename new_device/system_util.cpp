#include "system_util.h"

#include <regex>
#include <fstream>
#include <limits>
#include <cstdarg>
#include <iostream>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace tt::umd {

#define YEL "\e[0;33m"
#define RST "\e[0m"
#define LOG1(...) clr_printf("", __VA_ARGS__)  // Mostly debugging
#define LOG2(...) clr_printf("", __VA_ARGS__)  // Mostly debugging
#define WARN(...)  clr_printf(YEL, __VA_ARGS__)                       // Something wrong
#define ERROR(...) clr_printf(RED, __VA_ARGS__)                       // Something very bad

void clr_printf(const char *clr, const char *fmt, ...) {
    va_list args; va_start(args, fmt); printf ("%s", clr); vprintf(fmt, args); printf (RST); va_end(args);
}

// Hardcode (but allow override) of path now, to support environments with other 1GB hugepage mounts not for runtime.
const char* hugepage_dir_env = std::getenv("TT_BACKEND_HUGEPAGE_DIR");
std::string hugepage_dir = hugepage_dir_env ? hugepage_dir_env : "/dev/hugepages-1G";

const char device_name_pattern[] = "/dev/tenstorrent/%u";


// Looks for hugetlbfs inside /proc/mounts matching desired pagesize (typically 1G)
std::string find_hugepage_dir(std::size_t pagesize) {
    static const std::regex hugetlbfs_mount_re("^(nodev|hugetlbfs) (" + hugepage_dir + ") hugetlbfs ([^ ]+) 0 0$");
    static const std::regex pagesize_re("(?:^|,)pagesize=([0-9]+)([KMGT])(?:,|$)");

    std::ifstream proc_mounts("/proc/mounts");

    for (std::string line; std::getline(proc_mounts, line);) {
        if (std::smatch mount_match; std::regex_match(line, mount_match, hugetlbfs_mount_re)) {
            std::string options = mount_match[3];
            if (std::smatch pagesize_match; std::regex_search(options, pagesize_match, pagesize_re)) {
                std::size_t mount_page_size = std::stoull(pagesize_match[1]);
                switch (pagesize_match[2].str()[0]) {
                    case 'T': mount_page_size <<= 10;
                    case 'G': mount_page_size <<= 10;
                    case 'M': mount_page_size <<= 10;
                    case 'K': mount_page_size <<= 10;
                }

                if (mount_page_size == pagesize) {
                    return mount_match[2];
                }
            }
        }
    }

    WARN(
        "---- ttSiliconDevice::find_hugepage_dir: no huge page mount found in /proc/mounts for path: %s with "
        "hugepage_size: %d.\n",
        hugepage_dir.c_str(),
        pagesize);
    return std::string();
}


bool is_char_dev(const dirent *ent, const char *parent_dir) {
    if (ent->d_type == DT_UNKNOWN || ent->d_type == DT_LNK) {
        char name[2 * NAME_MAX + 2];
        strcpy(name, parent_dir);
        strcat(name, "/");
        strcat(name, ent->d_name);

        struct stat stat_result;
        if (stat(name, &stat_result) == -1) {
            return false;
        }

        return ((stat_result.st_mode & S_IFMT) == S_IFCHR);
    } else {
        return (ent->d_type == DT_CHR);
    }
}



std::vector<chip_id_t> ttkmd_scan() {

    static const char dev_dir[] = "/dev/tenstorrent";

    std::vector<chip_id_t> found_devices;

    DIR *d = opendir(dev_dir);
    if (d != nullptr) {
        while (true) {
            const dirent *ent = readdir(d);
            if (ent == nullptr) {
                break;
            }

            // strtoul allows initial whitespace, +, -
            if (!std::isdigit(ent->d_name[0])) {
                continue;
            }

            if (!is_char_dev(ent, dev_dir)) {
                continue;
            }

            char *endptr = nullptr;
            errno = 0;
            unsigned long index = std::strtoul(ent->d_name, &endptr, 10);
            if (index == std::numeric_limits<unsigned int>::max() && errno == ERANGE) {
                continue;
            }
            if (*endptr != '\0') {
                continue;
            }

            found_devices.push_back( (chip_id_t) index);
        }
        closedir(d);
    }

    std::sort(found_devices.begin(), found_devices.end());

    return found_devices;
}


int find_device(const uint16_t device_id) {
    // returns device id if found, otherwise -1
    char device_name[sizeof(device_name_pattern) + std::numeric_limits<unsigned int>::digits10];
    std::snprintf(device_name, sizeof(device_name), device_name_pattern, (unsigned int)device_id);
    int device_fd = ::open(device_name, O_RDWR | O_CLOEXEC);
    LOG2 ("find_device() open call returns device_fd: %d for device_name: %s (device_id: %d)\n", device_fd, device_name, device_id);
    return device_fd;
}


// For debug purposes when various stages fails.
void print_file_contents(std::string filename, std::string hint){
    if (std::filesystem::exists(filename)){
        std::ifstream meminfo(filename);
        if (meminfo.is_open()){
            std::cout << std::endl << "File " << filename << " " << hint << " is: " << std::endl;
            std::cout << meminfo.rdbuf();
        }
    }
}


}  // namespace tt::umd