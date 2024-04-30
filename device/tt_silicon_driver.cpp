// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <boost/interprocess/permissions.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <ratio>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <wait.h>
#include <errno.h>
#include <linux/pci.h>

#include "device/architecture.h"
#include "device/architecture_implementation.h"
#include "device/tlb.h"
#include "device/tt_arch_types.h"
#include "tt_device.h"
#include "kmdif.h"
#include "ioctl.h"

//#include "epoch_q.h"

#include <algorithm>
#include "yaml-cpp/yaml.h"
#include <filesystem>
#include <string.h>

#include <stdarg.h>
#include "device/cpuset_lib.hpp"
#include "common/logger.hpp"
#include "device/driver_atomics.h"

#define WHT "\e[0;37m"
#define BLK "\e[0;30m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define RST "\e[0m"
#define LOG1(...) if (g_DEBUG_LEVEL > 0) clr_printf("", __VA_ARGS__)  // Mostly debugging
#define LOG2(...) if (g_DEBUG_LEVEL > 1) clr_printf("", __VA_ARGS__)  // Mostly debugging
#define PRINT(...) clr_printf("",__VA_ARGS__)                       // What users should see
// #define LOG(...) if (false) clr_printf("", __VA_ARGS__)   // Mostly debugging
// #define PRINT(...) if (false) clr_printf(BLK, __VA_ARGS__)                       // What users should see
#define WARN(...)  clr_printf(YEL, __VA_ARGS__)                       // Something wrong
#define ERROR(...) clr_printf(RED, __VA_ARGS__)                       // Something very bad

using namespace boost::interprocess;
using namespace tt;
void clr_printf(const char *clr, const char *fmt, ...) {
    va_list args; va_start(args, fmt); printf ("%s", clr); vprintf(fmt, args); printf (RST); va_end(args);
}

int g_DEBUG_LEVEL; // /src/t6ifc/t6py/packages/tenstorrent/jlink/jtag_comm.cpp
bool g_READ_CHECKING_ENABLED = true;

bool g_USE_MSI_FOR_DMA = false; // Whether to wait for MSI after DMA transfer, or poll a variable
uint32_t g_DMA_BLOCK_SIZE_READ_THRESHOLD_BYTES = 0;  // 0 - never use DMA. Otherwise use DMA for all blocks larger than this size
uint32_t g_DMA_BLOCK_SIZE_WRITE_THRESHOLD_BYTES = 0; // 0 - never use DMA. Otherwise use DMA for all blocks larger than this size

// Address in CSM where the DMA request structure resides
uint32_t c_CSM_PCIE_CTRL_DMA_REQUEST_OFFSET = 0;
// Address where the trigger for transfer resides
uint32_t c_DMA_TRIGGER_ADDRESS = 0;
// To trigger arc interrupt
uint32_t c_ARC_MISC_CNTL_ADDRESS = 0;

// Print all buffers smaller than this number of bytes
uint32_t g_NUM_BYTES_TO_PRINT = 8;

// Workaround for tkmd < 1.21 use device_fd_per_host_ch[ch] instead of device_fd once per channel.
const bool g_SINGLE_PIN_PAGE_PER_FD_WORKAROND = true;
const uint32_t g_MAX_HOST_MEM_CHANNELS = 4;

volatile bool msi_interrupt_received = false;

const char device_name_pattern[] = "/dev/tenstorrent/%u";

const std::string tlb_large_read_mutex_name_prefix = "mem_tlb_large_read_mutex_pci_interface_id_";
const std::string tlb_large_write_mutex_name_prefix = "mem_tlb_large_write_mutex_pci_interface_id_";
const std::string tlb_small_read_write_mutex_name_prefix = "mem_tlb_small_read_write_mutex_pci_interface_id_";
const std::string arc_msg_mutex_name_prefix = "arc_msg_mutex_pci_interface_id_";

static uint32_t GS_BAR0_WC_MAPPING_SIZE = (156<<20) + (10<<21) + (18<<24);
static uint32_t BH_BAR0_WC_MAPPING_SIZE = 188<<21; // Defines the address for WC region. addresses 0 to BH_BAR0_WC_MAPPING_SIZE are in WC, above that are UC

static const uint32_t GS_WH_ARC_SCRATCH_6_OFFSET = 0x1FF30078;
static const uint32_t BH_NOC_NODE_ID_OFFSET = 0x1FD04044;

const uint32_t DMA_BUF_REGION_SIZE = 4 << 20;
const uint32_t HUGEPAGE_REGION_SIZE = 1 << 30; // 1GB
const uint32_t DMA_MAP_MASK = DMA_BUF_REGION_SIZE - 1;
const uint32_t HUGEPAGE_MAP_MASK = HUGEPAGE_REGION_SIZE - 1;

static const uint32_t MSG_ERROR_REPLY = 0xFFFFFFFF;

// Hardcode (but allow override) of path now, to support environments with other 1GB hugepage mounts not for runtime.
const char* hugepage_dir_env = std::getenv("TT_BACKEND_HUGEPAGE_DIR");
std::string hugepage_dir = hugepage_dir_env ? hugepage_dir_env : "/dev/hugepages-1G";

// Foward declarations
PCIdevice ttkmd_open(DWORD device_id, bool sharable /* = false */);
int ttkmd_close(struct PCIdevice &device);

uint32_t pcie_dma_transfer_turbo (TTDevice *dev, uint32_t chip_addr, uint32_t host_phys_addr, uint32_t size_bytes, bool write);
DMAbuffer pci_allocate_dma_buffer(TTDevice *dev, uint32_t size);
void pcie_init_dma_transfer_turbo (PCIdevice* dev);

// Stash all the fields of TTDevice in TTDeviceBase to make moving simpler.
struct TTDeviceBase
{
    unsigned int index;

    int device_fd = -1;
    std::vector<int> device_fd_per_host_ch;
    void *bar0_uc = nullptr;
    std::size_t bar0_uc_size = 0;
    std::size_t bar0_uc_offset = 0;

    void *bar0_wc = nullptr;
    std::size_t bar0_wc_size = 0;

    void *system_reg_mapping = nullptr;
    std::size_t system_reg_mapping_size;

    void *system_reg_wc_mapping = nullptr;
    std::size_t system_reg_wc_mapping_size;

    std::uint32_t system_reg_start_offset;  // Registers >= this are system regs, use the mapping.
    std::uint32_t system_reg_offset_adjust; // This is the offset of the first reg in the system reg mapping.

    int sysfs_config_fd = -1;
    std::uint16_t pci_domain;
    std::uint8_t pci_bus;
    std::uint8_t pci_device;
    std::uint8_t pci_function;

    unsigned int next_dma_buf = 0;

	DMAbuffer dma_completion_flag_buffer;  // When DMA completes, it writes to this buffer
	DMAbuffer dma_transfer_buffer;         // Buffer for large DMA transfers

    std::uint32_t max_dma_buf_size_log2;

    tenstorrent_get_device_info_out device_info;

    std::vector<DMAbuffer> dma_buffer_mappings;

    std::uint32_t read_checking_offset;
};

struct TTDevice : TTDeviceBase
{
    static TTDevice open(unsigned int device_id);
    void open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels);
    ~TTDevice() { reset(); }

    TTDevice(const TTDevice&) = delete;
    void operator = (const TTDevice&) = delete;

    TTDevice(TTDevice &&that) : TTDeviceBase(std::move(that)), arch(that.arch), architecture_implementation(std::move(that.architecture_implementation)) { that.drop(); }
    TTDevice &operator = (TTDevice &&that) {
        reset();

        *static_cast<TTDeviceBase*>(this) = std::move(that);
        arch = that.arch;
        architecture_implementation = std::move(that.architecture_implementation);
        that.drop();

        return *this;
    }

    void suspend_before_device_reset() {
        reset();
    }

    void resume_after_device_reset() {
        do_open();
    }

    tt::ARCH get_arch() const { return arch; }
    tt::umd::architecture_implementation* get_architecture_implementation() const { return architecture_implementation.get(); }

private:
    TTDevice() = default;

    void reset() {
        if (device_fd != -1) {
            close(device_fd);
        }

        if (bar0_wc != nullptr && bar0_wc != MAP_FAILED && bar0_wc != bar0_uc) {
            munmap(bar0_wc, bar0_wc_size);
        }

        if (bar0_uc != nullptr && bar0_uc != MAP_FAILED) {
            munmap(bar0_uc, bar0_uc_size);
        }

        if (system_reg_mapping != nullptr && system_reg_mapping != MAP_FAILED) {
            munmap(system_reg_mapping, system_reg_mapping_size);
        }

        for (auto &&buf : dma_buffer_mappings) {
            munmap(buf.pBuf, buf.size);
        }

        if (sysfs_config_fd != -1) {
            close(sysfs_config_fd);
        }

        drop();
    }

    void drop() {
        device_fd = -1;
        bar0_uc = nullptr;
        bar0_wc = nullptr;
        system_reg_mapping = nullptr;
        dma_buffer_mappings.clear();
        sysfs_config_fd = -1;
    }

    void do_open();

    tt::ARCH arch;
    std::unique_ptr<tt::umd::architecture_implementation> architecture_implementation;
};

TTDevice TTDevice::open(unsigned int device_id) {
    TTDevice ttdev;
    static int unique_id = 0;
    ttdev.index = device_id;
    ttdev.do_open();

    return ttdev;
}

bool is_grayskull(const uint16_t device_id) {
    return device_id == 0xfaca;
}

bool is_wormhole(const uint16_t device_id) {
    return device_id == 0x401e;
}

bool is_blackhole(const uint16_t device_id) {
    return device_id == 0xb140;
}

bool is_blackhole(const tenstorrent_get_device_info_out &device_info) {
    return is_blackhole(device_info.device_id);
}

bool is_wormhole(const tenstorrent_get_device_info_out &device_info) {
    return is_wormhole(device_info.device_id);
}

bool is_wormhole_b0(const uint16_t device_id, const uint16_t revision_id) {
    return (is_wormhole(device_id) && (revision_id == 0x01));
}


template <typename T>
void size_buffer_to_capacity(std::vector<T> &data_buf, std::size_t size_in_bytes) {
    std::size_t target_size = 0;
    if (size_in_bytes > 0) {
        target_size = ((size_in_bytes - 1) / sizeof(T)) + 1;
    }
    data_buf.resize(target_size);
}

// Get number of 1GB host hugepages installed. They are used for host queues.
uint32_t get_num_hugepages(){

    log_assert(HUGEPAGE_REGION_SIZE == 1 << 30, "Hugepages must be 1GB in size");
    std::string nr_hugepages_path = "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages";
    std::ifstream hugepages_file(nr_hugepages_path);
    uint32_t num_hugepages = 0;

    if(hugepages_file.is_open()) {
        std::string value;
        std::getline(hugepages_file, value);
        num_hugepages = std::stoi(value);
        log_debug(LogSiliconDriver, "Parsed num_hugepages: {} from {}", num_hugepages, nr_hugepages_path);
    } else {
        log_fatal("{} - Cannot open {}. errno: {}", __FUNCTION__, nr_hugepages_path, std::strerror(errno));
    }

    return num_hugepages;

}

// Dynamically figure out how many host memory channels (based on hugepages installed) for each device, based on arch.
uint32_t get_available_num_host_mem_channels(const uint32_t num_channels_per_device_target, const uint16_t device_id, const uint16_t revision_id) {

    // To minimally support hybrid dev systems with mix of ARCH, get only devices matching current ARCH's device_id.
    uint32_t total_num_tt_mmio_devices      = tt::cpuset::tt_cpuset_allocator::get_num_tt_pci_devices();
    uint32_t num_tt_mmio_devices_for_arch   = tt::cpuset::tt_cpuset_allocator::get_num_tt_pci_devices_by_pci_device_id(device_id, revision_id);
    uint32_t total_hugepages                = get_num_hugepages();

    // This shouldn't happen on silicon machines.
    if (num_tt_mmio_devices_for_arch == 0) {
        log_warning(LogSiliconDriver,
            "No TT devices found that match PCI device_id: 0x{:x} revision: {}, returning NumHostMemChannels:0",
            device_id, revision_id);
        return 0;
    }

    // GS will use P2P + 1 channel, others may support 4 host channels. Apply min of 1 to not completely break setups that were incomplete
    // ie fewer hugepages than devices, which would partially work previously for some devices.
    uint32_t num_channels_per_device_available = std::min(num_channels_per_device_target, std::max((uint32_t) 1, total_hugepages / num_tt_mmio_devices_for_arch));

    // Perform some helpful assertion checks to guard against common pitfalls that would show up as runtime issues later on.
    if (total_num_tt_mmio_devices > num_tt_mmio_devices_for_arch) {
        log_warning(LogSiliconDriver,
            "Hybrid system mixing different TTDevices - this is not well supported. Ensure sufficient Hugepages/HostMemChannels per device.");
    }

    if (total_hugepages < num_tt_mmio_devices_for_arch) {
        log_warning(LogSiliconDriver,
            "Insufficient NumHugepages: {} should be at least NumMMIODevices: {} for device_id: 0x{:x} revision: {}. NumHostMemChannels would be 0, bumping to 1.",
            total_hugepages, num_tt_mmio_devices_for_arch, device_id, revision_id);
    }

    if (num_channels_per_device_available < num_channels_per_device_target) {
        log_warning(LogSiliconDriver,
            "NumHostMemChannels: {} used for device_id: 0x{:x} less than target: {}. Workload will fail if it exceeds NumHostMemChannels. Increase Number of Hugepages.",
            num_channels_per_device_available, device_id, num_channels_per_device_target);
    }

    log_assert(num_channels_per_device_available <= g_MAX_HOST_MEM_CHANNELS,
        "NumHostMemChannels: {} exceeds supported maximum: {}, this is unexpected.",
        num_channels_per_device_available, g_MAX_HOST_MEM_CHANNELS);

    return num_channels_per_device_available;

}

int find_device(const uint16_t device_id) {
    // returns device id if found, otherwise -1
    char device_name[sizeof(device_name_pattern) + std::numeric_limits<unsigned int>::digits10];
    std::snprintf(device_name, sizeof(device_name), device_name_pattern, (unsigned int)device_id);
    int device_fd = ::open(device_name, O_RDWR | O_CLOEXEC);
    LOG2 ("find_device() open call returns device_fd: %d for device_name: %s (device_id: %d)\n", device_fd, device_name, device_id);
    return device_fd;
}

// Open a unique device_id per host memory channel (workaround for ttkmd < 1.21 support for more than 1 pin per fd)
void TTDevice::open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels) {
    for (int ch = 0; ch < num_host_mem_channels; ch++) {
        log_debug(LogSiliconDriver, "Opening device_fd_per_host_ch device index: {} ch: {} (num_host_mem_channels: {})", index, ch, num_host_mem_channels);
        int device_fd_for_host_mem = find_device(index);
        if (device_fd_for_host_mem == -1) {
            throw std::runtime_error(std::string("Failed opening a host memory device handle for device ") + std::to_string(index));
        }
        device_fd_per_host_ch.push_back(device_fd_for_host_mem);
    }
}

int get_revision_id(TTDevice *dev);

tt::ARCH detect_arch(TTDevice *dev) {
    if (is_grayskull(dev->device_info.device_id)) {
        return tt::ARCH::GRAYSKULL;
    } else if (is_wormhole_b0(dev->device_info.device_id, get_revision_id(dev))) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (is_wormhole(dev->device_info.device_id)) {
        return tt::ARCH::WORMHOLE;
    } else if (is_blackhole(dev->device_info.device_id)) {
        return tt::ARCH::BLACKHOLE;
    } else {
        throw std::runtime_error(std::string("Unknown device id."));
    }
}

tt::ARCH detect_arch(PCIdevice *pci_device) {
    return pci_device->hdev->get_arch();
}

tt::ARCH detect_arch(uint16_t device_id) {
    tt::ARCH arch_name = tt::ARCH::Invalid;
    if (find_device(device_id) == -1) {
        WARN("---- tt_SiliconDevice::detect_arch did not find silcon device_id: %d\n", device_id);
        return arch_name;
    }
    struct PCIdevice pci_device = ttkmd_open((DWORD)device_id, false);

    arch_name = detect_arch(&pci_device);

    ttkmd_close(pci_device);
    return arch_name;
}

void TTDevice::do_open() {
    device_fd = find_device(index);
    if (device_fd == -1) {
        throw std::runtime_error(std::string("Failed opening a handle for device ") + std::to_string(index));
    }

    tenstorrent_get_device_info device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.in.output_size_bytes = sizeof(device_info.out);

    if (ioctl(device_fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &device_info) == -1) {
        throw std::runtime_error(std::string("Get device info failed on device ") + std::to_string(index) + ".");
    }

    this->device_info = device_info.out;

    max_dma_buf_size_log2 = device_info.out.max_dma_buf_size_log2;

    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;

    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(device_fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(std::string("Query mappings failed on device ") + std::to_string(index) + ".");
    }

    tenstorrent_mapping bar0_uc_mapping;
    tenstorrent_mapping bar0_wc_mapping;
    tenstorrent_mapping bar2_uc_mapping;
    tenstorrent_mapping bar2_wc_mapping;

    memset(&bar0_uc_mapping, 0, sizeof(bar0_uc_mapping));
    memset(&bar0_wc_mapping, 0, sizeof(bar0_wc_mapping));
    memset(&bar2_uc_mapping, 0, sizeof(bar2_uc_mapping));
    memset(&bar2_wc_mapping, 0, sizeof(bar2_wc_mapping));

    for (unsigned int i = 0; i < mappings.query_mappings.in.output_mapping_count; i++) {
        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_UC) {
            bar0_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
            bar0_wc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_UC) {
            bar2_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_WC) {
            bar2_wc_mapping = mappings.mapping_array[i];
        }
    }

    if (bar0_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE0_UC) {
        throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR0 UC mapping.");
    }

    auto wc_mapping_size = is_blackhole(device_info.out) ? BH_BAR0_WC_MAPPING_SIZE : GS_BAR0_WC_MAPPING_SIZE;

    // Attempt WC mapping first so we can fall back to all-UC if it fails.
    if (bar0_wc_mapping.mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
        bar0_wc_size = std::min<size_t>(bar0_wc_mapping.mapping_size, wc_mapping_size);
        bar0_wc = mmap(NULL, bar0_wc_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar0_wc_mapping.mapping_base);
        if (bar0_wc == MAP_FAILED) {
            bar0_wc_size = 0;
            bar0_wc = nullptr;
        }
    }

    if (bar0_wc) {
        // The bottom part of the BAR is mapped WC. Map the top UC.
        bar0_uc_size = bar0_uc_mapping.mapping_size - wc_mapping_size;
        bar0_uc_offset = wc_mapping_size;
    } else {
        // No WC mapping, map the entire BAR UC.
        bar0_uc_size = bar0_uc_mapping.mapping_size;
        bar0_uc_offset = 0;
    }

    bar0_uc = mmap(NULL, bar0_uc_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar0_uc_mapping.mapping_base + bar0_uc_offset);

    if (bar0_uc == MAP_FAILED) {
        throw std::runtime_error(std::string("BAR0 UC memory mapping failed for device ") + std::to_string(index) + ".");
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (is_wormhole(device_info.out)) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR4 UC mapping.");
        }

        this->system_reg_mapping_size = bar2_uc_mapping.mapping_size;

        this->system_reg_mapping = mmap(NULL, bar2_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar2_uc_mapping.mapping_base);

        if (this->system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(std::string("BAR4 UC memory mapping failed for device ") + std::to_string(index) + ".");
        }

        this->system_reg_start_offset = (512 - 16) * 1024*1024;
        this->system_reg_offset_adjust = (512 - 32) * 1024*1024;
    }
    pci_domain = device_info.out.pci_domain;
    pci_bus = device_info.out.bus_dev_fn >> 8;
    pci_device = PCI_SLOT(device_info.out.bus_dev_fn);
    pci_function = PCI_FUNC(device_info.out.bus_dev_fn);

    arch = detect_arch(this);
    architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch));

    // GS+WH: ARC_SCRATCH[6], BH: NOC NODE_ID
    this->read_checking_offset = is_blackhole(device_info.out) ? BH_NOC_NODE_ID_OFFSET : GS_WH_ARC_SCRATCH_6_OFFSET;
}

void set_debug_level(int dl) {
    g_DEBUG_LEVEL = dl;
}

std::uint64_t pci_dma_buffer_get_physical_addr(DMAbuffer &dma_buffer) {
    log_assert (dma_buffer.pDma, "DMA Buffer not initialized");
    return reinterpret_cast<std::uint64_t>(dma_buffer.pDma);
}

std::uint64_t pci_dma_buffer_get_user_addr(DMAbuffer &dma_buffer) {
    log_assert (dma_buffer.pBuf, "DMA Buffer not initialized");
    return reinterpret_cast<std::uint64_t>(dma_buffer.pBuf);
}

DWORD ttkmd_init() { return 0; }    // 0 on success
DWORD ttkmd_uninit() { return 0; }  // 0 on success

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

int get_config_space_fd(TTDevice *dev) {
    if (dev->sysfs_config_fd == -1) {
        static const char pattern[] = "/sys/bus/pci/devices/0000:%02x:%02x.%u/config";
        char buf[sizeof(pattern)];
        std::snprintf(buf, sizeof(buf), pattern,
                      (unsigned int)dev->pci_bus, (unsigned int)dev->pci_device, (unsigned int)dev->pci_function);
        dev->sysfs_config_fd = open(buf, O_RDWR);

        if (dev->sysfs_config_fd == -1) {
            dev->sysfs_config_fd = open(buf, O_RDONLY);
        }
    }

    return dev->sysfs_config_fd;
}

int get_revision_id(TTDevice *dev) {

    static const char pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/revision";
    char buf[sizeof(pattern)];
    std::snprintf(buf, sizeof(buf), pattern,
    (unsigned int)dev->pci_domain, (unsigned int)dev->pci_bus, (unsigned int)dev->pci_device, (unsigned int)dev->pci_function);

    std::ifstream revision_file(buf);
    std::string revision_string;
    if (std::getline(revision_file, revision_string)) {
        return std::stoi(revision_string, nullptr, 0);
    } else {
        throw std::runtime_error("Revision ID read failed for device");
    }
}

int get_link_width(TTDevice *dev) {

    static const char pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/current_link_width";
    char buf[sizeof(pattern)];
    std::snprintf(buf, sizeof(buf), pattern,
    (unsigned int)dev->pci_domain, (unsigned int)dev->pci_bus, (unsigned int)dev->pci_device, (unsigned int)dev->pci_function);

    std::ifstream linkwidth_file(buf);
    std::string linkwidth_string;
    if (std::getline(linkwidth_file, linkwidth_string)) {
        return std::stoi(linkwidth_string, nullptr, 0);
    } else {
        throw std::runtime_error("Link width read failed for device");
    }
}

int get_link_speed(TTDevice *dev) {

    static const char pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/current_link_speed";
    char buf[sizeof(pattern)];
    std::snprintf(buf, sizeof(buf), pattern,
    (unsigned int)dev->pci_domain, (unsigned int)dev->pci_bus, (unsigned int)dev->pci_device, (unsigned int)dev->pci_function);

    std::ifstream linkspeed_file(buf);
    std::string linkspeed_string;
    int linkspeed;
    if (std::getline(linkspeed_file, linkspeed_string) && sscanf(linkspeed_string.c_str(), "%d", &linkspeed) == 1) {
        return linkspeed;
    } else {
        throw std::runtime_error("Link speed read failed for device");
    }
}

std::uint64_t read_bar0_base(TTDevice *dev) {
    const std::uint64_t bar_address_mask = ~(std::uint64_t)0xF;
    unsigned int bar0_config_offset = 0x10;

    std::uint64_t bar01;
    if (pread(get_config_space_fd(dev), &bar01, sizeof(bar01), bar0_config_offset) != sizeof(bar01)) {
        return 0;
    }

    return bar01 & bar_address_mask;
}

DMAbuffer allocate_dma_buffer(TTDevice *ttdev, unsigned int buffer_index, std::size_t size) {
    tenstorrent_allocate_dma_buf allocate_dma_buf;

    if (size > std::numeric_limits<decltype(allocate_dma_buf.in.requested_size)>::max()) {
        throw std::runtime_error(std::string("Requested DMA buffer size (" + std::to_string(allocate_dma_buf.in.requested_size)
                                             + ") bytes exceeds interface size limit for device " + std::to_string(ttdev->index) + ", with error: " + std::strerror(errno)));
    }

    memset(&allocate_dma_buf, 0, sizeof(allocate_dma_buf));
    allocate_dma_buf.in.requested_size = std::max<std::size_t>(size, getpagesize());
    allocate_dma_buf.in.buf_index = buffer_index;

    if (ioctl(ttdev->device_fd, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &allocate_dma_buf) == -1) {
        throw std::runtime_error(std::string("DMA buffer allocation failed (") + std::to_string(allocate_dma_buf.in.requested_size)
                                 + " bytes) for device " + std::to_string(ttdev->index) + ".");
    }

    void *mapping = mmap(NULL, allocate_dma_buf.out.size, PROT_READ | PROT_WRITE, MAP_SHARED, ttdev->device_fd, allocate_dma_buf.out.mapping_offset);

    log_trace(tt::LogSiliconDriver, "DMA buffer succeeded with size {} offset {} phy_addr {}", allocate_dma_buf.out.size, allocate_dma_buf.out.mapping_offset, allocate_dma_buf.out.physical_address);

    if (mapping == MAP_FAILED) {
        throw std::runtime_error(std::string("DMA buffer memory mapping failed for device ") + std::to_string(ttdev->index) + ".");
    }

    DMAbuffer dmabuf;
    dmabuf.pBuf = mapping;
    dmabuf.pDma = allocate_dma_buf.out.physical_address;
    dmabuf.size = allocate_dma_buf.out.size;

    ttdev->dma_buffer_mappings.push_back(dmabuf);

    return dmabuf;
}

PCIdevice ttkmd_open(DWORD device_id, bool sharable /* = false */)
{
    (void)sharable; // presently ignored

    auto ttdev = std::make_unique<TTDevice>(TTDevice::open(device_id));

    PCIdevice device;
    device.id = device_id;
    device.hdev = ttdev.get();
    device.vendor_id = ttdev->device_info.vendor_id;
    device.device_id = ttdev->device_info.device_id;
    device.subsystem_vendor_id = ttdev->device_info.subsystem_vendor_id;
    device.subsystem_id = ttdev->device_info.subsystem_id;
    device.dwBus = ttdev->pci_bus;
    device.dwSlot = ttdev->pci_device;
    device.dwFunction = ttdev->pci_function;
    device.BAR_addr = read_bar0_base(ttdev.get());
    device.BAR_size_bytes = ttdev->bar0_uc_size;
    device.revision_id = get_revision_id(ttdev.get());
    ttdev.release();

    return device;
}

int ttkmd_close(struct PCIdevice &device) {
    delete static_cast<TTDevice*>(device.hdev);

    return 0;
}

template <class T>
volatile T* register_address(const TTDevice *dev, std::uint32_t register_offset) {
    void *reg_mapping;
    if (dev->system_reg_mapping != nullptr && register_offset >= dev->system_reg_start_offset) {
        register_offset -= dev->system_reg_offset_adjust;
        reg_mapping = dev->system_reg_mapping;
    } else if (dev->bar0_wc != dev->bar0_uc && register_offset < dev->bar0_wc_size) {
        reg_mapping = dev->bar0_wc;
    } else {
        register_offset -= dev->bar0_uc_offset;
        reg_mapping = dev->bar0_uc;
    }

    return reinterpret_cast<T*>(static_cast<uint8_t*>(reg_mapping) + register_offset);
}

bool is_hardware_hung(const TTDevice *dev) {
    volatile const void *addr = reinterpret_cast<const char *>(dev->bar0_uc) + (dev->get_architecture_implementation()->get_arc_reset_scratch_offset() + 6 * 4) - dev->bar0_uc_offset;
    std::uint32_t scratch_data = *reinterpret_cast<const volatile std::uint32_t*>(addr);

    return (scratch_data == 0xffffffffu);
}

bool reset_by_sysfs(TTDevice *dev) {

    const char *virtual_env = getenv("VIRTUAL_ENV");
    if (virtual_env == nullptr)
        return false;

    std::string reset_helper_path = virtual_env;
    reset_helper_path += "/bin/reset-helper";

    std::string busid = std::to_string(dev->pci_bus);

    dev->suspend_before_device_reset();

    char *argv[3];
    argv[0] = const_cast<char*>(reset_helper_path.c_str());
    argv[1] = const_cast<char*>(busid.c_str());
    argv[2] = nullptr;

    pid_t reset_helper_pid;
    if (posix_spawn(&reset_helper_pid, reset_helper_path.c_str(), nullptr, nullptr, argv, environ) != 0)
        return false;

    siginfo_t reset_helper_status;
    if (waitid(P_PID, reset_helper_pid, &reset_helper_status, WEXITED) != 0)
        return false;

    if (reset_helper_status.si_status != 0)
        return false;

    dev->resume_after_device_reset();

    return true;
}

bool reset_by_ioctl(TTDevice *dev) {
    struct tenstorrent_reset_device reset_device;
    memset(&reset_device, 0, sizeof(reset_device));

    reset_device.in.output_size_bytes = sizeof(reset_device.out);
    reset_device.in.flags = 0;

    if (ioctl(dev->device_fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset_device) == -1) {
        return false;
    }

    return (reset_device.out.result == 0);
}

bool auto_reset_board(TTDevice *dev) {
    return ((reset_by_ioctl(dev) || reset_by_sysfs(dev)) && !is_hardware_hung(dev));
}

void detect_ffffffff_read(TTDevice *dev, std::uint32_t data_read = 0xffffffffu) {
    if (g_READ_CHECKING_ENABLED && data_read == 0xffffffffu && is_hardware_hung(dev)) {
        std::uint32_t scratch_data = *register_address<std::uint32_t>(dev, dev->read_checking_offset);

        if (auto_reset_board(dev)) {
            throw std::runtime_error("Read 0xffffffff from PCIE: auto-reset succeeded.");
        } else {
            throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
        }
    }
}

inline void record_access (const char* where, uint32_t addr, uint32_t size, bool turbo, bool write, bool block, bool endline) {
    LOG2 ("%s PCI_ACCESS %s 0x%8x  %8d bytes %s %s%s", where, write ? "WR" : "RD", addr, size, turbo ? "TU" : "  ", block ? "BLK" : "   ", endline ? "\n" : "" );
}

inline void print_buffer (const void* buffer_addr, uint32_t len_bytes = 16, bool endline = true) {
    // Prints each byte in a buffer
    if (g_DEBUG_LEVEL > 1) {
        uint8_t *b = (uint8_t *)(buffer_addr);
        for (uint32_t i = 0; i < len_bytes; i++) {
            LOG2 ("    [0x%x] = 0x%x (%u) ", i, b[i], b[i]);
        }
        if (endline) {
            LOG2 ("\n");
        }
    }
}

// Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
// Both routines assume that misaligned accesses are permitted on host memory.
//
// 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
// which glibc's memcpy may perform when unrolling. This affects from and to device.
// 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
// to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.

void memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the destination (device) pointer. If needed, do RMW to fix up the
    // first partial word.
    volatile copy_t *dp;

    std::uintptr_t dest_addr = reinterpret_cast<std::uintptr_t>(dest);
    unsigned int dest_misalignment = dest_addr % sizeof(copy_t);

    if (dest_misalignment != 0) {
        // Read-modify-write for the first dest element.
        dp = reinterpret_cast<copy_t*>(dest_addr - dest_misalignment);

        copy_t tmp = *dp;

        auto leading_len = std::min(sizeof(tmp) - dest_misalignment, num_bytes);

        std::memcpy(reinterpret_cast<char*>(&tmp) + dest_misalignment, src, leading_len);
        num_bytes -= leading_len;
        src = static_cast<const char *>(src) + leading_len;

        *dp++ = tmp;

    } else {
        dp = static_cast<copy_t*>(dest);
    }

    // Copy the destination-aligned middle.
    const copy_t *sp = static_cast<const copy_t*>(src);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++)
        *dp++ = *sp++;

    // Finally copy any sub-word trailer, again RMW on the destination.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *dp;

        std::memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

void memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t*>(src_addr - src_misalignment);

        copy_t tmp = *sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        std::memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
        num_bytes -= leading_len;
        dest = static_cast<char *>(dest) + leading_len;

    } else {
        sp = static_cast<const volatile copy_t*>(src);
    }

    // Copy the source-aligned middle.
    copy_t *dp = static_cast<copy_t *>(dest);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++)
        *dp++ = *sp++;

    // Finally copy any sub-word trailer.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *sp;
        std::memcpy(dp, &tmp, trailing_len);
    }
}

void read_block(TTDevice *dev, uint32_t byte_addr, uint32_t num_bytes, uint8_t* buffer_addr, uint32_t dma_buf_size) {
    if (num_bytes >= g_DMA_BLOCK_SIZE_READ_THRESHOLD_BYTES && g_DMA_BLOCK_SIZE_READ_THRESHOLD_BYTES > 0) {
        record_access ("read_block_a", byte_addr, num_bytes, true, false, true, true); // addr, size, turbo, write, block, endline

        DMAbuffer &transfer_buffer = dev->dma_transfer_buffer;

        uint64_t host_phys_addr = pci_dma_buffer_get_physical_addr (transfer_buffer);
        uint64_t host_user_addr = pci_dma_buffer_get_user_addr (transfer_buffer);
        while (num_bytes > 0) {
            uint32_t transfered_bytes = std::min<uint32_t>(num_bytes, dma_buf_size);
            pcie_dma_transfer_turbo (dev, byte_addr, host_phys_addr, transfered_bytes, false);
            memcpy (buffer_addr, (void*)host_user_addr, transfered_bytes);
            num_bytes -= transfered_bytes;
            byte_addr += transfered_bytes;
            buffer_addr += transfered_bytes;
        }
        return;
    }

    record_access("read_block_b", byte_addr, num_bytes, false, false, true, false); // addr, size, turbo, write, block, endline

    void *reg_mapping;
    if (dev->system_reg_mapping != nullptr && byte_addr >= dev->system_reg_start_offset) {
        byte_addr -= dev->system_reg_offset_adjust;
        reg_mapping = dev->system_reg_mapping;
    } else if (dev->bar0_wc != dev->bar0_uc && byte_addr < dev->bar0_wc_size) {
        reg_mapping = dev->bar0_wc;
    } else {
        byte_addr -= dev->bar0_uc_offset;
        reg_mapping = dev->bar0_uc;
    }

    const void *src = reinterpret_cast<const char *>(reg_mapping) + byte_addr;
    void *dest = reinterpret_cast<void *>(buffer_addr);

#ifndef DISABLE_ISSUE_3487_FIX
    memcpy_from_device(dest, src, num_bytes);
#else
#ifdef FAST_MEMCPY

    if ((num_bytes % 32 == 0) && ((intptr_t(dest) & 31) == 0) && ((intptr_t(src) & 31) == 0))
    memcpy_from_device(dest, src, num_bytes);
    {
        // Faster memcpy version.. about 8x currently compared to pci_read above
        fastMemcpy(dest, src, num_bytes);
    }
    else
#else
    // ~4x faster than pci_read above, but works for all sizes and alignments
    memcpy(dest, src, num_bytes);
#endif
#endif

    if (num_bytes >= sizeof(std::uint32_t)) {
        detect_ffffffff_read(dev, *reinterpret_cast<std::uint32_t*>(dest));
    }
    print_buffer (buffer_addr, std::min(g_NUM_BYTES_TO_PRINT, num_bytes), true);
}

void write_block(TTDevice *dev, uint32_t byte_addr, uint32_t num_bytes, const uint8_t* buffer_addr, uint32_t dma_buf_size) {
    if (num_bytes >= g_DMA_BLOCK_SIZE_WRITE_THRESHOLD_BYTES && g_DMA_BLOCK_SIZE_WRITE_THRESHOLD_BYTES > 0) {
        record_access ("write_block_a", byte_addr, num_bytes, true, true, true, true); // addr, size, turbo, write, block, endline

        DMAbuffer &transfer_buffer = dev->dma_transfer_buffer;

        uint64_t host_phys_addr = pci_dma_buffer_get_physical_addr (transfer_buffer);
        uint64_t host_user_addr = pci_dma_buffer_get_user_addr (transfer_buffer);
        while (num_bytes > 0) {
            uint32_t transfered_bytes = std::min<uint32_t>(num_bytes, dma_buf_size);
            memcpy ( (void*)host_user_addr, buffer_addr, transfered_bytes);
            pcie_dma_transfer_turbo (dev, byte_addr, host_phys_addr, transfered_bytes, true);
            num_bytes -= transfered_bytes;
            byte_addr += transfered_bytes;
            buffer_addr += transfered_bytes;
        }
        return;
    }

    record_access("write_block_b", byte_addr, num_bytes, false, true, true, false); // addr, size, turbo, write, block, endline

    void *reg_mapping;
    if (dev->system_reg_mapping != nullptr && byte_addr >= dev->system_reg_start_offset) {
        byte_addr -= dev->system_reg_offset_adjust;
        reg_mapping = dev->system_reg_mapping;
    } else if (dev->bar0_wc != dev->bar0_uc && byte_addr < dev->bar0_wc_size) {
        reg_mapping = dev->bar0_wc;
    } else {
        byte_addr -= dev->bar0_uc_offset;
        reg_mapping = dev->bar0_uc;
    }

    void *dest = reinterpret_cast<char *>(reg_mapping) + byte_addr;
    const void *src = reinterpret_cast<const void *>(buffer_addr);
#ifndef DISABLE_ISSUE_3487_FIX
    memcpy_to_device(dest, src, num_bytes);
#else
#ifdef FAST_MEMCPY
    memcpy_to_device(dest, src, num_bytes);
   if ((num_bytes % 32 == 0) && ((intptr_t(dest) & 31) == 0) && ((intptr_t(src) & 31) == 0))

   {
      // Faster memcpy version.. about 8x currently compared to pci_read above
      fastMemcpy(dest, src, num_bytes);
   }
   else
#else
     // ~4x faster than pci_read above, but works for all sizes and alignments
     memcpy(dest, src, num_bytes);
#endif
#endif
    print_buffer (buffer_addr, std::min(g_NUM_BYTES_TO_PRINT, num_bytes), true);
}

void read_checking_enable(bool enable = true) {
    g_READ_CHECKING_ENABLED = enable;
}

// Read/write to the configuration space of the device
// pData is a pointer to a buffer (see memory module)
DWORD read_cfg(TTDevice *dev, DWORD byte_offset, uint64_t pData, DWORD num_bytes) {

    if (pread(get_config_space_fd(dev), reinterpret_cast<void*>(pData), num_bytes, byte_offset) != num_bytes) {
        throw std::runtime_error("Config space read failed for device ");
    }

    return 0;
}

DWORD write_cfg(TTDevice *dev, DWORD byte_offset, uint64_t pData, DWORD num_bytes) {

    if (pwrite(get_config_space_fd(dev), reinterpret_cast<const void*>(pData), num_bytes, byte_offset) != num_bytes) {
        throw std::runtime_error("Config space read failed for device ");
    }

    return 0;
}

DMAbuffer pci_allocate_dma_buffer(TTDevice *dev, uint32_t size) {

    uint32_t page_size = getpagesize();
    uint32_t page_aligned_size = (size + page_size - 1) & ~(page_size - 1);

    DMAbuffer ret_val = allocate_dma_buffer(dev, dev->next_dma_buf++, page_aligned_size);
    LOG1 ("Allocated DMA buffer at 0x%lx 0x%lx size: %u\n", ret_val.pBuf, ret_val.pDma, size);
    return ret_val;
}

void pcie_init_dma_transfer_turbo (PCIdevice* dev) {
    // From SHA 8cf7ff1bc7b3886a:
    if (detect_arch(dev) == tt::ARCH::WORMHOLE_B0) {
        c_CSM_PCIE_CTRL_DMA_REQUEST_OFFSET = 0x1fef84c8; // chip.AXI.get_path_info("ARC_CSM.ARC_PCIE_DMA_REQUEST")
    } else {
        c_CSM_PCIE_CTRL_DMA_REQUEST_OFFSET = 0x1fef84c0; // chip.AXI.get_path_info("ARC_CSM.ARC_PCIE_DMA_REQUEST")
    }
    c_DMA_TRIGGER_ADDRESS = 0x1ff30074;              // chip.AXI.get_path_info("ARC_RESET.SCRATCH[5]")
    c_ARC_MISC_CNTL_ADDRESS = 0x1ff30100;            // chip.AXI.get_path_info("ARC_RESET.ARC_MISC_CNTL")
}

void set_use_dma(bool msi, uint32_t dma_block_size_read_threshold_bytes, uint32_t dma_block_size_write_threshold_bytes) {
    g_USE_MSI_FOR_DMA = msi;
    g_DMA_BLOCK_SIZE_READ_THRESHOLD_BYTES  = dma_block_size_read_threshold_bytes;
    g_DMA_BLOCK_SIZE_WRITE_THRESHOLD_BYTES = dma_block_size_write_threshold_bytes;
}

void write_regs(TTDevice *dev, uint32_t byte_addr, uint32_t word_len, const void *data) {
    record_access("write_regs", byte_addr, word_len * sizeof(uint32_t), false, true, false, false);

    volatile uint32_t *dest = register_address<std::uint32_t>(dev, byte_addr);
    const uint32_t *src = reinterpret_cast<const uint32_t*>(data);

    while (word_len-- != 0) {
        uint32_t temp;
        memcpy(&temp, src++, sizeof(temp));
        *dest++ = temp;
    }
    LOG2(" REG ");
    print_buffer (data, std::min(g_NUM_BYTES_TO_PRINT, word_len * 4), true);
}

void write_tlb_reg(TTDevice *dev, uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size) {
    record_access("write_tlb_reg", byte_addr, tlb_cfg_reg_size, false, true, false, false);

    log_assert((tlb_cfg_reg_size == 8) or (tlb_cfg_reg_size == 12), "Tenstorrent hardware supports only 64bit or 96bit TLB config regs");

    volatile uint64_t *dest_qw = register_address<std::uint64_t>(dev, byte_addr);
    volatile uint32_t *dest_extra_dw = register_address<std::uint32_t>(dev, byte_addr+8);
#if defined(__ARM_ARCH) || defined(__riscv)
    // The store below goes through UC memory on x86, which has implicit ordering constraints with WC accesses.
    // ARM has no concept of UC memory. This will not allow for implicit ordering of this store wrt other memory accesses.
    // Insert an explicit full memory barrier for ARM.
    // Do the same for RISC-V.
    tt_driver_atomics::mfence();
#endif
    *dest_qw = value_lower;
    if (tlb_cfg_reg_size > 8) {
        uint32_t* p_value_upper = reinterpret_cast<uint32_t*>(&value_upper);
        *dest_extra_dw = p_value_upper[0];
    }
    tt_driver_atomics::mfence(); // Otherwise subsequent WC loads move earlier than the above UC store to the TLB register.

    LOG2(" TLB ");
    print_buffer (&value_lower, sizeof(value_lower), true);
    if (tlb_cfg_reg_size > 8) {
        uint32_t* p_value_upper = reinterpret_cast<uint32_t*>(&value_upper);
        print_buffer (p_value_upper, sizeof(uint32_t), true);
    }
}

void read_regs(TTDevice *dev, uint32_t byte_addr, uint32_t word_len, void *data) {
    record_access("read_regs", byte_addr, word_len * sizeof(uint32_t), false, false, false, false);

    const volatile uint32_t *src = register_address<std::uint32_t>(dev, byte_addr);
    uint32_t *dest = reinterpret_cast<uint32_t*>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
    LOG2(" REG ");
    print_buffer (data, std::min(g_NUM_BYTES_TO_PRINT, word_len * 4), true);
}

void handle_dma_timeout(TTDevice *dev, uint32_t size_bytes, bool write) {
    detect_ffffffff_read(dev);
    throw std::runtime_error(std::string("DMA transfer timeout: ")
                             + std::to_string(size_bytes)
                             + (write ? " byte write." : " byte read."));
}
uint32_t pcie_dma_transfer_turbo (TTDevice *dev, uint32_t chip_addr, uint32_t host_phys_addr, uint32_t size_bytes, bool write) {
    // c_timer t ("");

    // t.now_in ("1. DMA setup");

    if (c_CSM_PCIE_CTRL_DMA_REQUEST_OFFSET == 0) {
        throw std::runtime_error ("pcie_init_dma_transfer_turbo must be called before pcie_dma_transfer_turbo");
    }

    arc_pcie_ctrl_dma_request_t req = {
        .chip_addr           = chip_addr,
        .host_phys_addr      = host_phys_addr,
        .completion_flag_phys_addr = static_cast<uint32_t>(pci_dma_buffer_get_physical_addr(dev->dma_completion_flag_buffer)),
        .size_bytes          = size_bytes,
        .write               = (write ? 1U : 0U),
        .pcie_msi_on_done    = g_USE_MSI_FOR_DMA ? 1U : 0U,
        .pcie_write_on_done  = g_USE_MSI_FOR_DMA ? 0U : 1U,
        .trigger             = 1U,
        .repeat              = 1
    };

    volatile uint32_t *complete_flag = (uint32_t *)pci_dma_buffer_get_user_addr(dev->dma_completion_flag_buffer);
    *complete_flag = 0;

    // Configure the DMA engine
    msi_interrupt_received = false;
    write_regs (dev, c_CSM_PCIE_CTRL_DMA_REQUEST_OFFSET, sizeof(req) / sizeof(uint32_t), &req);

    // Trigger ARC interrupt 0 on core 0
    int arc_misc_cntl_value = 0;

    // NOTE: Ideally, we should read the state of this register before writing to it, but that
    //       casues a lot of delay (reads have huge latencies)
    arc_misc_cntl_value |= (1 << 16); // Cause IRQ0 on core 0
    write_regs (dev, c_ARC_MISC_CNTL_ADDRESS, 1, &arc_misc_cntl_value);

    if (!g_USE_MSI_FOR_DMA) {
        // t.now_in ("2. DMA poll");
        int wait_loops = 0;
        while (true) {
            // The complete flag is set ty by ARC (see src/hardware/soc/tb/arc_fw/lib/pcie_dma.c)
            if (*complete_flag == 0xfaca) break;
            wait_loops++;
        }
        // LOG2 ("Waited %d iterations\n", wait_loops);
    } else {
        // t.now_in ("2. DMA wait for MSI");
        while (msi_interrupt_received == false)
            ;
    }

    return 0; // TODO: status
}

void print_device_info (struct PCIdevice &d) {
    LOG1("PCIEIntfId   0x%x\n", d.id);
    LOG1("VID:DID      0x%x:0x%x\n", d.vendor_id, d.device_id);
    LOG1("SubVID:SubID 0x%x:0x%x\n", d.subsystem_vendor_id, d.subsystem_id);
    LOG1("BSF          %x:%x:%x\n",  d.dwBus, d.dwSlot, d.dwFunction);
    LOG1("BAR          0x%llx  size: %dMB\n",    d.BAR_addr, d.BAR_size_bytes / 1024 / 1024);
}

// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------

#include "tt_silicon_driver_common.hpp"
#include "tt_xy_pair.h"
#include <thread>
#include <fstream>
#include <iomanip>

struct dynamic_tlb {
    uint32_t bar_offset;        // Offset that address is mapped to, within the PCI BAR.
    uint32_t remaining_size;    // Bytes remaining between bar_offset and end of the TLB.
};

struct routing_cmd_t {
    uint64_t sys_addr;
    uint32_t data;
    uint32_t flags;
    uint16_t rack;
    uint16_t src_resp_buf_index;
    uint32_t local_buf_index;
    uint8_t  src_resp_q_id;
    uint8_t  host_mem_txn_id;
    uint16_t padding;
    uint32_t src_addr_tag; //upper 32-bits of request source address.
};

struct remote_update_ptr_t{
  uint32_t ptr;
  uint32_t pad[3];
};

namespace {
    struct tt_4_byte_aligned_buffer {
        // Stores a 4 byte aligned buffer
        // If the input buffer is already 4 byte aligned, this is a nop
        std::uint32_t* local_storage = nullptr;
        std::uint32_t input_size = 0;
        std::uint32_t block_size = 0;

        tt_4_byte_aligned_buffer(const void* mem_ptr, uint32_t size_in_bytes) {
            input_size = size_in_bytes;
            local_storage = (uint32_t*)mem_ptr;
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            uint32_t aligned_size = (size_in_bytes + alignment_mask) & ~alignment_mask;

            if(size_in_bytes < aligned_size) {
                local_storage = new uint32_t[aligned_size / sizeof(uint32_t)];
            }
            block_size = aligned_size;
        }

        ~tt_4_byte_aligned_buffer() {
            if(block_size > input_size) {
                delete [] local_storage;
            }
        }
    };
}
// Get TLB index (from zero), check if it's in 16MB, 2MB or 1MB TLB range, and dynamically program it.
dynamic_tlb set_dynamic_tlb(PCIdevice* dev, unsigned int tlb_index, tt_xy_pair start, tt_xy_pair end,
                            std::uint64_t address, bool multicast, std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>>& harvested_coord_translation, std::uint64_t ordering) {
    auto architecture_implementation = dev->hdev->get_architecture_implementation();
    if (multicast) {
        std::tie(start, end) = architecture_implementation->multicast_workaround(start, end);
    }

    LOG2("set_dynamic_tlb with arguments: tlb_index = %d, start = (%d, %d), end = (%d, %d), address = 0x%x, multicast = %d, ordering = %d\n",
         tlb_index, start.x, start.y, end.x, end.y, address, multicast, (int)ordering);

    tt::umd::tlb_configuration tlb_config = architecture_implementation->get_tlb_configuration(tlb_index);
    std::uint32_t TLB_CFG_REG_SIZE_BYTES = architecture_implementation->get_tlb_cfg_reg_size_bytes();
    auto translated_start_coords = harvested_coord_translation.at(dev -> logical_id).at(start);
    auto translated_end_coords = harvested_coord_translation.at(dev -> logical_id).at(end);
    uint32_t tlb_address    = address / tlb_config.size;
    uint32_t local_offset   = address % tlb_config.size;
    uint32_t tlb_base       = tlb_config.base + (tlb_config.size * tlb_config.index_offset);
    uint32_t tlb_cfg_reg    = tlb_config.cfg_addr + (TLB_CFG_REG_SIZE_BYTES * tlb_config.index_offset);

    std::pair<std::uint64_t, std::uint64_t> tlb_data = TLB_DATA {
        .local_offset = tlb_address,
        .x_end = static_cast<uint64_t>(translated_end_coords.x),
        .y_end = static_cast<uint64_t>(translated_end_coords.y),
        .x_start = static_cast<uint64_t>(translated_start_coords.x),
        .y_start = static_cast<uint64_t>(translated_start_coords.y),
        .mcast = multicast,
        .ordering = ordering,
        .static_vc = true,
    }.apply_offset(tlb_config.offset);

    LOG1 ("set_dynamic_tlb() with tlb_index: %d tlb_index_offset: %d dynamic_tlb_size: %dMB tlb_base: 0x%x tlb_cfg_reg: 0x%x\n", tlb_index, tlb_config.index_offset, tlb_config.size/(1024*1024), tlb_base, tlb_cfg_reg);
    //write_regs(dev -> hdev, tlb_cfg_reg, 2, &tlb_data);
    write_tlb_reg(dev->hdev, tlb_cfg_reg, tlb_data.first, tlb_data.second, TLB_CFG_REG_SIZE_BYTES);

    return { tlb_base + local_offset, tlb_config.size - local_offset };
}


dynamic_tlb set_dynamic_tlb(PCIdevice *dev, unsigned int tlb_index, tt_xy_pair target, std::uint64_t address, std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>>& harvested_coord_translation, std::uint64_t ordering = TLB_DATA::Relaxed) {
    return set_dynamic_tlb(dev, tlb_index, tt_xy_pair(0, 0), target, address, false, harvested_coord_translation, ordering);
}

dynamic_tlb set_dynamic_tlb_broadcast(PCIdevice *dev, unsigned int tlb_index, std::uint64_t address, std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>>& harvested_coord_translation, tt_xy_pair start, tt_xy_pair end, std::uint64_t ordering = TLB_DATA::Relaxed) {
    // Issue a broadcast to cores included in the start (top left) and end (bottom right) grid
    return set_dynamic_tlb (dev, tlb_index, start, end,
                            address, true, harvested_coord_translation, ordering);
}

bool tt_SiliconDevice::address_in_tlb_space(uint32_t address, uint32_t size_in_bytes, int32_t tlb_index, uint32_t tlb_size, std::uint32_t chip) {
    return ((tlb_config_map.at(chip).find(tlb_index) != tlb_config_map.at(chip).end()) && address >= tlb_config_map.at(chip).at(tlb_index) && (address + size_in_bytes <= tlb_config_map.at(chip).at(tlb_index) + tlb_size));
}

tt_SocDescriptor& tt_SiliconDevice::get_soc_descriptor(chip_id_t chip_id){
    return soc_descriptor_per_chip.at(chip_id);
}

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_SiliconDevice::get_virtual_soc_descriptors() {
    return soc_descriptor_per_chip;
}

void tt_SiliconDevice::initialize_interprocess_mutexes(int pci_interface_id, bool cleanup_mutexes_in_shm) {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of time here (during device init)
    // since its unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is responsible for initializing the driver with this
    // field set to cleanup after an aborted process.

    // Store old mask and clear processes umask
    auto old_umask = umask(0);
    permissions unrestricted_permissions;
    unrestricted_permissions.set_unrestricted();
    std::string mutex_name = "";

    // Initialize Dynamic TLB mutexes
    for(auto &tlb : dynamic_tlb_config) {
        mutex_name = tlb.first + std::to_string(pci_interface_id);
        if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
        hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);
    }

    // Initialize ARC core mutex
    mutex_name = "ARC_MSG" + std::to_string(pci_interface_id);
    if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
    hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);

    if (arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
        mutex_name = NON_MMIO_MUTEX_NAME + std::to_string(pci_interface_id);
        // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for ethernet broadcast
        if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
        hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic
    mutex_name = MEM_BARRIER_MUTEX_NAME + std::to_string(pci_interface_id);
    if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
    hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);
    
    // Restore old mask
    umask(old_umask);
}

void tt_SiliconDevice::create_device(const std::unordered_set<chip_id_t> &target_mmio_device_ids, const uint32_t &num_host_mem_ch_per_mmio_device, const bool skip_driver_allocs, const bool clean_system_resources) {
    m_pci_log_level = 0;
    m_dma_buf_size = 0;
    LOG1("---- tt_SiliconDevice::tt_SiliconDevice\n");
    static int unique_driver_id = 0;
    driver_id = unique_driver_id++;

    // Set the log level for debugging
    const char* pci_log_level = std::getenv("TT_PCI_LOG_LEVEL");
    if (pci_log_level) {
        m_pci_log_level = atoi (pci_log_level);
    }
    set_debug_level(m_pci_log_level);
    LOG1 ("TT_PCI_LOG_LEVEL=%d\n", m_pci_log_level);

    const char* dma_buf_size = std::getenv("TT_PCI_DMA_BUF_SIZE");
    if (dma_buf_size) {
        m_dma_buf_size = atoi (dma_buf_size);
    }
    LOG1 ("TT_PCI_DMA_BUF_SIZE=%d\n", m_dma_buf_size);

    // Don't buffer stdout.
    setbuf(stdout, NULL);

    // Just use PCI interface id from physical_device_id given by cluster desc mmio map. For GS, already virtualized to use available devices.
    auto logical_to_physical_device_id_map = ndesc->get_chips_with_mmio();

    log_assert(target_mmio_device_ids.size() > 0, "Must provide set of target_mmio_device_ids to tt_SiliconDevice constructor now.");

    for (const chip_id_t &logical_device_id : target_mmio_device_ids) {
        m_pci_device_map.insert({logical_device_id, new struct PCIdevice});
        struct PCIdevice* pci_device = m_pci_device_map.at(logical_device_id);

        log_assert(logical_to_physical_device_id_map.count(logical_device_id) != 0, "Cannot find logical mmio device_id: {} in cluster desc / logical-to-physical-map", logical_device_id);
        int pci_interface_id = logical_to_physical_device_id_map.at(logical_device_id);

        log_debug(LogSiliconDriver, "Opening TT_PCI_INTERFACE_ID {} for netlist target_device_id: {}", pci_interface_id, logical_device_id);
        *pci_device = ttkmd_open ((DWORD) pci_interface_id, false);
        pci_device->logical_id = logical_device_id;

        // MT: Initial BH
        if (arch_name == tt::ARCH::BLACKHOLE) {
            m_num_host_mem_channels = 0;
        } else {
            m_num_host_mem_channels = get_available_num_host_mem_channels(num_host_mem_ch_per_mmio_device, pci_device->device_id, pci_device->revision_id);
        }

        log_debug(LogSiliconDriver, "Using {} Hugepages/NumHostMemChannels for TTDevice (logical_device_id: {} pci_interface_id: {} device_id: 0x{:x} revision: {})",
            m_num_host_mem_channels, logical_device_id, pci_interface_id, pci_device->device_id, pci_device->revision_id);

        if (g_SINGLE_PIN_PAGE_PER_FD_WORKAROND) {
            pci_device->hdev->open_hugepage_per_host_mem_ch(m_num_host_mem_channels);
        }

        // Initialize these. Used to be in header file.
        for (int ch = 0; ch < g_MAX_HOST_MEM_CHANNELS; ch ++) {
            hugepage_mapping[logical_device_id][ch]= nullptr;
            hugepage_mapping_size[logical_device_id][ch] = 0;
            hugepage_physical_address[logical_device_id][ch] = 0;
        }

        initialize_interprocess_mutexes(pci_interface_id, clean_system_resources);

        if (!skip_driver_allocs)
            print_device_info (*pci_device);

        // MT: Initial BH - hugepages will fail init
        // For using silicon driver without workload to query mission mode params, no need for hugepage/dmabuf.
        if (!skip_driver_allocs){
            bool hugepages_initialized = init_hugepage(logical_device_id);
            // Large writes to remote chips require hugepages to be initialized.
            // Conservative assert - end workload if remote chips present but hugepages not initialized (failures caused if using remote only for small transactions)
            if(target_remote_chips.size()) {
                log_assert(hugepages_initialized, "Hugepages must be successfully initialized if workload contains remote chips!");
            }
            uint16_t channel = 0; // Single channel sufficient for this?
            if (not hugepage_mapping.at(logical_device_id).at(channel)) {
                init_dmabuf(logical_device_id);
            }
        }
        harvested_coord_translation.insert({logical_device_id, create_harvested_coord_translation(arch_name, true)}); //translation layer for harvested coords. Default is identity map
        archs_in_cluster.push_back(detect_arch(logical_to_physical_device_id_map.at(logical_device_id)));
    }

    for(const chip_id_t& chip : target_devices_in_cluster) {
        // Initialize identity mapping for Non-MMIO chips as well
        if(!ndesc -> is_chip_mmio_capable(chip)) {
            harvested_coord_translation.insert({chip, create_harvested_coord_translation(arch_name, true)});
        }
    }
}

bool tt_SiliconDevice::noc_translation_en() {
    return translation_tables_en;
}
bool tt_SiliconDevice::using_harvested_soc_descriptors() {
    return perform_harvesting_on_sdesc && performed_harvesting;
}

std::unordered_map<tt_xy_pair, tt_xy_pair> tt_SiliconDevice::get_harvested_coord_translation_map(chip_id_t logical_device_id) {
    return harvested_coord_translation.at(logical_device_id);
}

std::unordered_map<chip_id_t, uint32_t> tt_SiliconDevice::get_harvesting_masks_for_soc_descriptors() {
    if(using_harvested_soc_descriptors()) {
        return harvested_rows_per_target;
    }
    std::unordered_map<chip_id_t, uint32_t> default_harvesting_masks = {};
    for(const auto chip : target_devices_in_cluster) default_harvesting_masks.insert({chip, 0});
    return default_harvesting_masks;
}

tt_SiliconDevice::tt_SiliconDevice(const std::string &sdesc_path, const std::string &ndesc_path, const std::set<chip_id_t> &target_devices, 
                                   const uint32_t &num_host_mem_ch_per_mmio_device, const std::unordered_map<std::string, std::int32_t>& dynamic_tlb_config_, 
                                   const bool skip_driver_allocs, const bool clean_system_resources, bool perform_harvesting, std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks) : tt_device(sdesc_path) {
    std::unordered_set<chip_id_t> target_mmio_device_ids;
    target_devices_in_cluster = target_devices;
    arch_name = tt_SocDescriptor(sdesc_path).arch;
    perform_harvesting_on_sdesc = perform_harvesting;

    auto available_device_ids = detect_available_device_ids();
    m_num_pci_devices = available_device_ids.size();

    if (!skip_driver_allocs)
        log_info(LogSiliconDriver, "Detected {} PCI device{} : {}", m_num_pci_devices, (m_num_pci_devices > 1) ? "s":"", available_device_ids);

    if (ndesc_path == "") {
        ndesc = tt_ClusterDescriptor::create_for_grayskull_cluster(target_devices, available_device_ids);
    }
    else {
        ndesc = tt_ClusterDescriptor::create_from_yaml(ndesc_path);
    }

    for (auto &d: target_devices){
        if (ndesc->is_chip_mmio_capable(d)){
            target_mmio_device_ids.insert(d);
        }
        else {
            target_remote_chips.insert(d);
        }
    }
    dynamic_tlb_config = dynamic_tlb_config_;

    // It is mandatory for all devices to have these TLBs set aside, as the driver needs them to issue remote reads and writes.
    auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
    dynamic_tlb_config["LARGE_READ_TLB"] =  architecture_implementation->get_mem_large_read_tlb();
    dynamic_tlb_config["LARGE_WRITE_TLB"] = architecture_implementation->get_mem_large_write_tlb();

    for(const auto& tlb : dynamic_tlb_config) {
        dynamic_tlb_ordering_modes.insert({tlb.first, TLB_DATA::Relaxed}); // All dynamic TLBs use Relaxed Ordering by default; MT: Good for BH
    }
    create_device(target_mmio_device_ids, num_host_mem_ch_per_mmio_device, skip_driver_allocs, clean_system_resources);

    // MT: Initial BH - Disable dependency to ethernet firmware
    if(arch_name == tt::ARCH::BLACKHOLE) {
        use_ethernet_ordered_writes = false;
        use_ethernet_broadcast = false;
        use_virtual_coords_for_eth_broadcast = false;
    }

    if(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
        const auto& harvesting_masks = ndesc -> get_harvesting_info();
        const auto& noc_translation_enabled = ndesc -> get_noc_translation_table_en();

        translation_tables_en = false;
        for(auto& masks : harvesting_masks) {
            if(target_devices.find(masks.first) != target_devices.end()) {
                harvested_rows_per_target[masks.first] = get_harvested_noc_rows(masks.second);
                noc_translation_enabled_for_chip[masks.first] = noc_translation_enabled.at(masks.first);
                num_rows_harvested.insert({masks.first, std::bitset<32>(masks.second).count()});
                if(harvested_rows_per_target[masks.first]) {
                    performed_harvesting = true;
                }
            }
        }
        if(noc_translation_enabled_for_chip.size() > 0) {
            auto const consistent_translation_table_state = [&] (std::pair<chip_id_t, bool> const& i) {
                return noc_translation_enabled_for_chip.begin() -> second == i.second;
            };

            bool translation_tables_match_on_all_chips = std::all_of(noc_translation_enabled_for_chip.begin(), noc_translation_enabled_for_chip.end(), consistent_translation_table_state);
            log_assert(translation_tables_match_on_all_chips, "Cluster uses NOC translation tables inconsistently across chips.");
            translation_tables_en = noc_translation_enabled_for_chip.begin() -> second;
        }

        if(translation_tables_en) {
            harvested_coord_translation.clear();
            for(const chip_id_t& chip : target_devices_in_cluster) {
                harvested_coord_translation.insert({chip, create_harvested_coord_translation(arch_name, false)});
            }
        }
        log_assert(performed_harvesting ? translation_tables_en : true, "Using a harvested WH cluster with NOC translation disabled.");
    }
    else if(arch_name == tt::ARCH::BLACKHOLE) {
        // Default harvesting info for Blackhole, describing no harvesting
        for(auto chip_id = target_devices.begin(); chip_id != target_devices.end(); chip_id++){
            harvested_rows_per_target[*chip_id] =  0; //get_harvested_noc_rows_for_chip(*chip_id);
            num_rows_harvested.insert({*chip_id, 0}); // Only set for broadcast TLB to get RISCS out of reset. We want all rows to have a reset signal sent.
            if(harvested_rows_per_target[*chip_id]) {
                performed_harvesting = true;
            }
        }
    }
    else if(arch_name == tt::ARCH::GRAYSKULL) {
        // Multichip harvesting is supported for GS.
        for(auto chip_id = target_devices.begin(); chip_id != target_devices.end(); chip_id++){
            harvested_rows_per_target[*chip_id] =  get_harvested_noc_rows_for_chip(*chip_id);
            num_rows_harvested.insert({*chip_id, 0}); // Only set for broadcast TLB to get RISCS out of reset. We want all rows to have a reset signal sent.
            if(harvested_rows_per_target[*chip_id]) {
                performed_harvesting = true;
            }
        }
    }

    if(simulated_harvesting_masks.size()) {
        performed_harvesting = true;
        for (auto device_id = target_devices.begin(); device_id != target_devices.end(); device_id++) {
            log_assert(simulated_harvesting_masks.find(*device_id) != simulated_harvesting_masks.end(), "Could not find harvesting mask for device_id {}", *device_id);
            if(arch_name == tt::ARCH::GRAYSKULL) {
                log_assert((simulated_harvesting_masks.at(*device_id) & harvested_rows_per_target[*device_id]) == harvested_rows_per_target[*device_id],
                            "Simulated harvesting config for device {} does not include the actual harvesting config (real config must be contained in simulated config when running on device). Actual Harvested Rows : {}    Simulated Harvested Rows : {}",
                            *device_id,  harvested_rows_per_target[*device_id], simulated_harvesting_masks.at(*device_id));

            }
            else if(arch_name == tt::ARCH::WORMHOLE_B0 || arch_name == tt::ARCH::WORMHOLE) {
                log_assert(std::bitset<32>(simulated_harvesting_masks.at(*device_id)).count() >= std::bitset<32>(harvested_rows_per_target[*device_id]).count(),
                            "Simulated Harvesting for WH must contain at least as many rows as the actual harvesting config. Actual Harvested Rows : {}  Simulated Harvested Rows : {}",
                            harvested_rows_per_target[*device_id], simulated_harvesting_masks.at(*device_id));
                            num_rows_harvested.at(*device_id) = std::bitset<32>(simulated_harvesting_masks.at(*device_id)).count();
            }
            harvested_rows_per_target[*device_id] = simulated_harvesting_masks.at(*device_id);
            if(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
                log_assert(performed_harvesting ? translation_tables_en : true, "Using a harvested WH cluster with NOC translation disabled.");
            }
        }
    }

    perform_harvesting_and_populate_soc_descriptors(sdesc_path, perform_harvesting);
    populate_cores();

    // MT: Initial BH - skip this for BH
    if(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
        remote_transfer_ethernet_cores.resize(target_mmio_device_ids.size());
        for (const auto &logical_mmio_chip_id : target_mmio_device_ids) {
            tt_SocDescriptor& soc_desc = get_soc_descriptor(logical_mmio_chip_id);
            // 4-5 is for send_epoch_commands, 0-3 are for everything else
            for (std::uint32_t i = 0; i < NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS; i++) {
                if(remote_transfer_ethernet_cores.size() <= logical_mmio_chip_id) {
                    remote_transfer_ethernet_cores.resize(logical_mmio_chip_id + 1);
                }
                remote_transfer_ethernet_cores.at(logical_mmio_chip_id).push_back(
                    tt_cxy_pair(logical_mmio_chip_id, soc_desc.ethernet_cores.at(i).x, soc_desc.ethernet_cores.at(i).y)
                );
            }
        }
    }
}

void tt_SiliconDevice::configure_active_ethernet_cores_for_mmio_device(chip_id_t mmio_chip, const std::unordered_set<tt_xy_pair>& active_eth_cores_per_chip) {
    // Makes UMD aware of which ethernet cores have active links.
    // Based on this information, UMD determines which ethernet cores can be used for host->cluster non-MMIO transfers.
    // This overrides the default ethernet cores tagged for host to cluster routing in the constructor and must be called for all MMIO devices, if default behaviour
    // is not desired.
    log_assert(get_soc_descriptor(mmio_chip).arch == tt::ARCH::WORMHOLE_B0, "{} can only be called for Wormhole arch", __FUNCTION__);
    auto& eth_cores = get_soc_descriptor(mmio_chip).ethernet_cores;
    // Cores 0, 1, 6, 7 are only available if in the active set
    static std::unordered_set<tt_xy_pair> eth_cores_available_if_active = {eth_cores.at(0), eth_cores.at(1), eth_cores.at(6), eth_cores.at(7)};
    // Eth cores 8 and 9 are always available
    std::vector<tt_cxy_pair> non_mmio_access_cores_for_chip = {tt_cxy_pair(mmio_chip, eth_cores.at(8)), tt_cxy_pair(mmio_chip, eth_cores.at(9))};
    for (const auto& active_eth_core : active_eth_cores_per_chip) {
        if (eth_cores_available_if_active.find(active_eth_core) != eth_cores_available_if_active.end()) {
            non_mmio_access_cores_for_chip.push_back(tt_cxy_pair(mmio_chip, active_eth_core));
        }
    }

    remote_transfer_ethernet_cores[mmio_chip] = non_mmio_access_cores_for_chip;
    active_eth_core_idx_per_chip.insert({mmio_chip, 0});
    non_mmio_transfer_cores_customized = true;
}

void tt_SiliconDevice::populate_cores() {
    std::uint32_t count = 0;
    for(const auto chip : soc_descriptor_per_chip) {
        workers_per_chip.insert({chip.first, std::unordered_set<tt_xy_pair>(chip.second.workers.begin(), chip.second.workers.end())});
        if(count == 0) {
            eth_cores = std::unordered_set<tt_xy_pair>(chip.second.ethernet_cores.begin(), chip.second.ethernet_cores.end());
            for(std::uint32_t dram_idx = 0; dram_idx < chip.second.get_num_dram_channels(); dram_idx++) {
                dram_cores.insert(chip.second.get_core_for_dram_channel(dram_idx, 0)) ;
            }
        }
        count++;
    }
}

std::unordered_map<chip_id_t, uint32_t> tt_SiliconDevice::get_harvesting_masks_from_harvested_rows(std::unordered_map<chip_id_t, std::vector<uint32_t>> harvested_rows) {
    std::unordered_map<chip_id_t, uint32_t> harvesting_masks = {};
    for(const auto& chip : harvested_rows) {
        uint32_t harvesting_mask_per_chip = 0;
        harvesting_masks.insert({chip.first, 0});
        for(const auto& row : chip.second) {
            harvesting_masks.at(chip.first) |= (1 << row);
        }
    }
    return harvesting_masks;
}
std::vector<int> tt_SiliconDevice::extract_rows_to_remove(const tt::ARCH &arch, const int worker_grid_rows, const int harvested_rows) {
    // Check if harvesting config is legal for GS and WH
    log_assert(!((harvested_rows & 1) || (harvested_rows & 64) || (harvested_rows & 0xFFFFF000)), "For grayskull and wormhole, only rows 1-5 and 7-11 can be harvested");
    std::vector<int> row_coordinates_to_remove;
    int row_coordinate = 0;
    int tmp = harvested_rows;
    while (tmp) {
        if (tmp & 1)
            row_coordinates_to_remove.push_back(row_coordinate);

        tmp = tmp >> 1;
        row_coordinate++;
    }
    if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        // For Wormhole, we always remove the last few rows in the SOC descriptor in case of harvesting
        for (int i = 0; i < row_coordinates_to_remove.size(); i++) {
            row_coordinates_to_remove[i] = worker_grid_rows - i;
        }
    }
    return row_coordinates_to_remove;
}

void tt_SiliconDevice::remove_worker_row_from_descriptor(tt_SocDescriptor& full_soc_descriptor, const std::vector<int>& row_coordinates_to_remove) {
    std::vector<tt_xy_pair> workers_to_keep;
    for(auto worker = (full_soc_descriptor.workers).begin(); worker != (full_soc_descriptor.workers).end(); worker++){
        if(find(row_coordinates_to_remove.begin(), row_coordinates_to_remove.end(), (*worker).y) == row_coordinates_to_remove.end()){
            workers_to_keep.push_back(*worker);
        }
        else{
            (full_soc_descriptor.harvested_workers).push_back(*worker);
            full_soc_descriptor.cores.at(*worker).type = CoreType::HARVESTED;
        }
    }
    full_soc_descriptor.workers = workers_to_keep;
    (full_soc_descriptor.worker_grid_size).y -= row_coordinates_to_remove.size();
    full_soc_descriptor.routing_y_to_worker_y = {};
    full_soc_descriptor.worker_log_to_routing_y = {};

    std::set<int> modified_y_coords = {};

    for(const auto& core : full_soc_descriptor.workers) {
        modified_y_coords.insert(core.y);
    }
    int logical_y_coord = 0;
    for(const auto& y_coord : modified_y_coords) {
        full_soc_descriptor.routing_y_to_worker_y.insert({y_coord, logical_y_coord});
        full_soc_descriptor.worker_log_to_routing_y.insert({logical_y_coord,  y_coord});
        logical_y_coord++;
    }
}

void tt_SiliconDevice::harvest_rows_in_soc_descriptor(tt::ARCH arch, tt_SocDescriptor& sdesc, uint32_t harvested_rows) {
    std::uint32_t max_row_to_remove = (*std::max_element((sdesc.workers).begin(), (sdesc.workers).end(), [] (const auto& a, const auto& b) { return a.y < b.y; })).y;
    std::vector<int> row_coordinates_to_remove = extract_rows_to_remove(arch, max_row_to_remove, harvested_rows);
    remove_worker_row_from_descriptor(sdesc, row_coordinates_to_remove);
}

void tt_SiliconDevice::perform_harvesting_and_populate_soc_descriptors(const std::string& sdesc_path, const bool perform_harvesting) {
    const auto default_sdesc = tt_SocDescriptor(sdesc_path);
    for(const auto& chip : harvested_rows_per_target) {
        auto temp_sdesc = default_sdesc;
        if(perform_harvesting) {
            harvest_rows_in_soc_descriptor(arch_name, temp_sdesc, chip.second);
        }
        soc_descriptor_per_chip.insert({chip.first, temp_sdesc});
    }
}

void tt_SiliconDevice::check_pcie_device_initialized(int device_id) {

    struct PCIdevice* pci_device = get_pci_device(device_id);
    if (arch_name == tt::ARCH::GRAYSKULL) {
        if (!is_grayskull(pci_device->device_id)) {
            throw std::runtime_error("Attempted to run grayskull configured tt_device on " + get_arch_str(detect_arch(pci_device)));
        }
    }
    else if (arch_name == tt::ARCH::WORMHOLE || arch_name == tt::ARCH::WORMHOLE_B0) {
        if (!is_wormhole(pci_device->device_id)) {
            throw std::runtime_error("Attempted to run wormhole configured tt_device on " + get_arch_str(detect_arch(pci_device)));
        }
    }
    else if (arch_name == tt::ARCH::BLACKHOLE) {
        if (!is_blackhole(pci_device->device_id)) {
            throw std::runtime_error("Attempted to run blackhole configured tt_device on " + get_arch_str(detect_arch(pci_device)));
        }
    }
    else {
        throw std::runtime_error("Unsupported architecture: " + get_arch_str(arch_name));
    }
    auto architecture_implementation = pci_device->hdev->get_architecture_implementation();

    // MT Initial BH - Add check for blackhole once access to ARC registers is setup through TLBs
    if (arch_name != tt::ARCH::BLACKHOLE) {
        LOG1 ("== Check if device_id: %d is initialized\n", device_id);
        uint32_t bar_read_initial = bar_read32(device_id, architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4);
        uint32_t arg = bar_read_initial == 500 ? 325 : 500;
        uint32_t bar_read_again;
        uint32_t arc_msg_return = arc_msg(device_id, 0xaa00 | architecture_implementation->get_arc_message_test(), true, arg, 0, 1, &bar_read_again);
        if (arc_msg_return != 0 || bar_read_again != arg + 1) {
            auto postcode = bar_read32(device_id, architecture_implementation->get_arc_reset_scratch_offset());
            throw std::runtime_error("Device is not initialized: arc_fw postcode: " + std::to_string(postcode)
            + " arc_msg_return: " + std::to_string(arc_msg_return)
            + " arg: " + std::to_string(arg)
            + " bar_read_initial: " + std::to_string(bar_read_initial)
            + " bar_read_again: " + std::to_string(bar_read_again));
        }
    }


    if (test_setup_interface()) {
        throw std::runtime_error("Device is incorrectly initialized. If this is a harvested Wormhole machine, it is likely that NOC Translation Tables are not enabled on device. These need to be enabled for the silicon driver to run.");
    }
}

std::unordered_map<tt_xy_pair, tt_xy_pair> tt_SiliconDevice::create_harvested_coord_translation(const tt::ARCH arch, bool identity_map) {
    log_assert(identity_map ? true : (arch != tt::ARCH::GRAYSKULL), "NOC Translation can only be performed for WH devices");
    std::unordered_map<tt_xy_pair, tt_xy_pair> translation_table = {};

    tt_xy_pair grid_size;
    std::vector<uint32_t> T6_x = {};
    std::vector<uint32_t> T6_y = {};
    std::vector<tt_xy_pair> ethernet = {};
    // Store device specific data for GS and WH depending on arch
    if(arch == tt::ARCH::GRAYSKULL) {
        grid_size = tt_xy_pair(13, 12);
        T6_x = {12, 1, 11, 2, 10, 3, 9, 4, 8, 5, 7, 6};
        T6_y = {11, 1, 10, 2, 9, 3, 8, 4, 7, 5};
    }
    else if (arch == tt::ARCH::BLACKHOLE) {
        grid_size = tt_xy_pair(17, 12);
        T6_x = {16, 1, 15, 2, 14, 3, 13, 4, 12, 5, 11, 6, 10, 7};
        T6_y = {11, 2, 10, 3, 9, 4, 8, 5, 7, 6};
    }
    else {
        grid_size = tt_xy_pair(10, 12);
        T6_x = {1, 2, 3, 4, 6, 7, 8, 9};
        T6_y = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
        ethernet = {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {6, 6}, {7, 6}, {8, 6}, {9, 6}};
    }

    
    if(identity_map) {
        // When device is initialized, assume no harvesting and create an identity map for cores
        // This flow is always used for GS, since there is no hardware harvesting
        for(int x = 0; x < grid_size.x; x++) {
            for(int y = 0; y < grid_size.y; y++) {
                tt_xy_pair curr_core = tt_xy_pair(x, y);
                translation_table.insert({curr_core, curr_core});
            }
        }
        return translation_table;
    }

    // If this function is called with identity_map = false, we have perform NOC translation
    // This can only happen for WH devices
    // Setup coord translation for workers. Map all worker cores
    for(int x = 0; x < grid_size.x; x++) {
        for(int y = 0; y < grid_size.y; y++) {
            tt_xy_pair curr_core = tt_xy_pair(x, y);

            if(std::find(T6_x.begin(), T6_x.end(), x) != T6_x.end() &&
            std::find(T6_y.begin(), T6_y.end(), y) != T6_y.end()) {
                // This is a worker core. Apply translation for WH.
                tt_xy_pair harvested_worker;
                if(x >= 1 && x <= 4) harvested_worker.x = x + 17;
                else if(x <= 9 && x > 5) harvested_worker.x = x + 16;
                else log_assert(false, "Invalid WH worker x coord {} when creating translation tables.", x);

                if(y >= 1 && y <= 5) harvested_worker.y = y + 17;
                else if(y <= 11 && y > 6) harvested_worker.y = y + 16;
                else log_assert(false, "Invalid WH worker y coord {} when creating translation tables.", y);
                translation_table.insert({curr_core, harvested_worker});
            }

            else if(std::find(ethernet.begin(), ethernet.end(), curr_core) != ethernet.end()){
                // This is an eth core. Apply translation for WH.
                tt_xy_pair harvested_eth_core;
                if(x >= 1 && x <= 4) harvested_eth_core.x = x + 17;
                else if(x <= 9 && x > 5) harvested_eth_core.x = x + 16;
                else log_assert(false, "Invalid WH eth_core x coord {} when creating translation tables.", x);

                if(y == 0) harvested_eth_core.y = y + 16;
                else if(y == 6) harvested_eth_core.y = y + 11;
                else log_assert(false, "Invalid WH eth_core y coord {} when creating translation tables.", y);
                translation_table.insert({curr_core, harvested_eth_core});
            }

            else {
                // All other cores for WH are not translated in case of harvesting.
                translation_table.insert({curr_core, curr_core});
            }
        }
    }
    return translation_table;
}

void tt_SiliconDevice::translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {
    auto translated_coords = harvested_coord_translation[device_id].at(tt_xy_pair(c, r));
    c = translated_coords.x;
    r = translated_coords.y;
}

void tt_SiliconDevice::initialize_pcie_devices() {
    LOG1("---- tt_SiliconDevice::start\n");

    for (auto &device_it : m_pci_device_map){
        check_pcie_device_initialized(device_it.first);
    }

    // If requires multi-channel or doesn't support mmio-p2p, init iatus without p2p.
    // MT Initial BH - skip device to host related init
    if (arch_name != tt::ARCH::BLACKHOLE) {
        if (m_num_host_mem_channels > 1 || arch_name != tt::ARCH::GRAYSKULL) {
            init_pcie_iatus_no_p2p();
        } else {
            init_pcie_iatus();
        }
    }
    init_membars();
    
    // https://yyz-gitlab.local.tenstorrent.com/ihamer/ll-sw/issues/25
    // Note: using pcie dma while device is idle is safe, mixing p2p is unsafe, see issue above
    // TODO: disable pcie dma if p2p traffic is present, ie. chip-to-chip or chip-to-host

    for (auto &device_it : m_pci_device_map){
        struct PCIdevice* pci_device = device_it.second;
        auto device_id = pci_device->device_id;
        // MT Initial BH - Don't use PCIe DMA
        bool enable_pcie_dma;
        if (arch_name == tt::ARCH::BLACKHOLE) {
            enable_pcie_dma = false;
        } else {
            enable_pcie_dma = m_dma_buf_size>0;
        }
        // Use DMA only for transfers that cross the size thresholds (empirically determined)
        if (enable_pcie_dma) {
            try {
                log_trace(LogSiliconDriver, "Enable PCIE DMA with bufsize {}", m_dma_buf_size);
                set_use_dma (false, 128, 0); // use dma for reads only
                init_dma_turbo_buf(pci_device);
            } catch (const std::exception &e) {
                log_trace(LogSiliconDriver, "Disable PCIE DMA, fallback to MMIO transfers due to exepction {}", e.what());
                set_use_dma (false, 0, 0);
                uninit_dma_turbo_buf(pci_device);
            }
        } else {
            log_trace(LogSiliconDriver, "Disable PCIE DMA");
        }
    }   
}

void tt_SiliconDevice::broadcast_pcie_tensix_risc_reset(struct PCIdevice *device, const TensixSoftResetOptions &soft_resets) {
    LOG1("---- tt_SiliconDevice::broadcast_tensix_risc_reset\n");

    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;

    LOG1("== For all tensix set soft-reset for %s risc cores.\n", TensixSoftResetOptionsToString(valid).c_str());

    auto architecture_implementation = device->hdev->get_architecture_implementation();
    auto [soft_reset_reg, _] = set_dynamic_tlb_broadcast(device, architecture_implementation->get_reg_tlb(), architecture_implementation->get_tensix_soft_reset_addr(), harvested_coord_translation, tt_xy_pair(0, 0), 
                                tt_xy_pair(architecture_implementation->get_grid_size_x() - 1, architecture_implementation->get_grid_size_y() - 1 - num_rows_harvested.at(device -> logical_id)), TLB_DATA::Posted);
    write_regs(device->hdev, soft_reset_reg, 1, &valid);
    tt_driver_atomics::sfence();
}

std::set<chip_id_t> tt_SiliconDevice::get_target_mmio_device_ids() {
    if(!all_target_mmio_devices.size()) {
        for (const auto &it: m_pci_device_map) {
            all_target_mmio_devices.insert(it.first);
        }
    }
    return all_target_mmio_devices;
}

void tt_SiliconDevice::assert_risc_reset() {
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);
}

void tt_SiliconDevice::deassert_risc_reset() {
    broadcast_tensix_risc_reset_to_cluster(TENSIX_DEASSERT_SOFT_RESET);
}

void tt_SiliconDevice::deassert_risc_reset_at_core(tt_cxy_pair core) {
    std::uint32_t target_device = core.chip; // Get Target Device to query soc descriptor and determine location in cluster
    log_assert(std::find(get_soc_descriptor(target_device).workers.begin(), get_soc_descriptor(target_device).workers.end(), core) != get_soc_descriptor(target_device).workers.end() ||
               std::find(get_soc_descriptor(target_device).ethernet_cores.begin(), get_soc_descriptor(target_device).ethernet_cores.end(), core) != get_soc_descriptor(target_device).ethernet_cores.end(),
                                "Cannot deassert reset on a non-tensix or harvested core");
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(target_device);
    if(target_is_mmio_capable) {
        log_assert(m_pci_device_map.find(target_device) != m_pci_device_map.end(), "Could not find MMIO mapped device in devices connected over PCIe");
        send_tensix_risc_reset_to_core(core, TENSIX_DEASSERT_SOFT_RESET);
    }
    else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Can't issue access to remote core in BH");
        send_remote_tensix_risc_reset_to_core(core, TENSIX_DEASSERT_SOFT_RESET);
    }
}

void tt_SiliconDevice::assert_risc_reset_at_core(tt_cxy_pair core) {
    std::uint32_t target_device = core.chip; // Get Target Device to query soc descriptor and determine location in cluster
    log_assert(std::find(get_soc_descriptor(target_device).workers.begin(), get_soc_descriptor(target_device).workers.end(), core) != get_soc_descriptor(target_device).workers.end() ||
               std::find(get_soc_descriptor(target_device).ethernet_cores.begin(), get_soc_descriptor(target_device).ethernet_cores.end(), core) != get_soc_descriptor(target_device).ethernet_cores.end(),
                                "Cannot assert reset on a non-tensix or harvested core");
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(target_device);
    if(target_is_mmio_capable) {
        log_assert(m_pci_device_map.find(target_device) != m_pci_device_map.end(), "Could not find MMIO mapped device in devices connected over PCIe");
        send_tensix_risc_reset_to_core(core, TENSIX_ASSERT_SOFT_RESET);
    }
    else {
        send_remote_tensix_risc_reset_to_core(core, TENSIX_ASSERT_SOFT_RESET);
    }
}

// Free memory during teardown, and remove (clean/unlock) from any leftover mutexes.
void tt_SiliconDevice::cleanup_shared_host_state() {
    for(auto &mutex : hardware_resource_mutex_map) {
        mutex.second.reset();
        mutex.second = nullptr;
        named_mutex::remove(mutex.first.c_str());
    }
}

std::unordered_set<chip_id_t> tt_SiliconDevice::get_all_chips_in_cluster() {
    return ndesc -> get_all_chips();
}
int tt_SiliconDevice::get_number_of_chips_in_cluster() {
    // Returns the number of chips seen in the network descriptor
    return ndesc -> get_all_chips().size();
}

tt_ClusterDescriptor* tt_SiliconDevice::get_cluster_description() {return ndesc.get();}
// Can be used before instantiating a silicon device
int tt_SiliconDevice::detect_number_of_chips() {

    auto available_device_ids = detect_available_device_ids();
    return available_device_ids.size();

}

// Can be used before instantiating a silicon device
std::vector<chip_id_t> tt_SiliconDevice::detect_available_device_ids() {

    std::vector<chip_id_t> detected_device_ids = ttkmd_scan();
    return detected_device_ids;
}

static bool check_dram_core_exists(const std::vector<std::vector<tt_xy_pair>> &all_dram_cores, tt_xy_pair target_core) {
    bool dram_core_exists = false;
    for (const auto &dram_cores_in_channel : all_dram_cores) {
        for (auto dram_core : dram_cores_in_channel) {
            if (dram_core.x == target_core.x && dram_core.y == target_core.y) {
                return true;
            }
        }
    }
    return false;
}

std::function<void(uint32_t, uint32_t, const uint8_t*, uint32_t)> tt_SiliconDevice::get_fast_pcie_static_tlb_write_callable(int device_id) {
    struct PCIdevice* pci_device = get_pci_device(device_id);
    TTDevice* dev = pci_device->hdev;

    const auto callable = [dev](uint32_t byte_addr, uint32_t num_bytes, const uint8_t* buffer_addr, uint32_t dma_buf_size) {
        write_block(dev, byte_addr, num_bytes, buffer_addr, dma_buf_size);
    };

    return callable;
}

void tt_SiliconDevice::write_device_memory(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair target, std::uint32_t address, const std::string& fallback_tlb) {
    struct PCIdevice* pci_device = get_pci_device(target.chip);
    TTDevice *dev = pci_device->hdev;

    const uint8_t* buffer_addr = static_cast<const uint8_t*>(mem_ptr);

    // LOG1("---- tt_SiliconDevice::write_device_memory to chip:%lu %lu-%lu at 0x%x size_in_bytes: %d small_access: %d\n",
    //     target.chip, target.x, target.y, address, size_in_bytes, small_access);

    std::int32_t tlb_index = 0;
    std::optional<std::tuple<std::uint32_t, std::uint32_t>> tlb_data = std::nullopt;
    if(tlbs_init) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Pre-initialized TLBs not supported in BH");
        tlb_index = map_core_to_tlb(tt_xy_pair(target.x, target.y));
        tlb_data = dev->get_architecture_implementation()->describe_tlb(tlb_index);
    }

    if (tlb_data.has_value() && address_in_tlb_space(address, size_in_bytes, tlb_index, std::get<1>(tlb_data.value()), target.chip)) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Pre-initialized TLBs not supported in BH");
        auto [tlb_offset, tlb_size] = tlb_data.value();
        write_block(dev, tlb_offset + address % tlb_size, size_in_bytes, buffer_addr, m_dma_buf_size);
    } else {
        const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
        const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device -> id));

        while(size_in_bytes > 0) {

            auto [mapped_address, tlb_size] = set_dynamic_tlb(pci_device, tlb_index, target, address, harvested_coord_translation, dynamic_tlb_ordering_modes.at(fallback_tlb));
            uint32_t transfer_size = std::min(size_in_bytes, tlb_size);
            write_block(dev, mapped_address, transfer_size, buffer_addr, m_dma_buf_size);

            size_in_bytes -= transfer_size;
            address += transfer_size;
            buffer_addr += transfer_size;
        }
        // LOG1 ("Write done Dynamic TLB with pid=%ld\n", (long)getpid());
    }
}

void tt_SiliconDevice::read_device_memory(void *mem_ptr, tt_cxy_pair target, std::uint32_t address, std::uint32_t size_in_bytes, const std::string& fallback_tlb) {
    // Assume that mem_ptr has been allocated adequate memory on host when this function is called. Otherwise, this function will cause a segfault.
    LOG1("---- tt_SiliconDevice::read_device_memory to chip:%lu %lu-%lu at 0x%x size_in_bytes: %d\n", target.chip, target.x, target.y, address, size_in_bytes);
    struct PCIdevice* pci_device = get_pci_device(target.chip);
    TTDevice *dev = pci_device->hdev;

    uint8_t* buffer_addr = static_cast<uint8_t*>(mem_ptr);

    std::int32_t tlb_index = 0;
    std::optional<std::tuple<std::uint32_t, std::uint32_t>> tlb_data = std::nullopt;
    if(tlbs_init) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Pre-initialized TLBs not supported in BH");
        tlb_index = map_core_to_tlb(tt_xy_pair(target.x, target.y));
        tlb_data = dev->get_architecture_implementation()->describe_tlb(tlb_index);
    }
    LOG1("  tlb_index: %d, tlb_data.has_value(): %d\n", tlb_index, tlb_data.has_value());

    if (tlb_data.has_value()  && address_in_tlb_space(address, size_in_bytes, tlb_index, std::get<1>(tlb_data.value()), target.chip)) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Pre-initialized TLBs not supported in BH");    // MT: Use only dynamic TLBs and never program static
        auto [tlb_offset, tlb_size] = tlb_data.value();
        read_block(dev, tlb_offset + address % tlb_size, size_in_bytes, buffer_addr, m_dma_buf_size);
        LOG1 ("  read_block called with tlb_offset: %d, tlb_size: %d\n", tlb_offset, tlb_size);
    } else {
        const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
        const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device -> id));
        LOG1 ("  dynamic tlb_index: %d\n", tlb_index);
        while(size_in_bytes > 0) {

            auto [mapped_address, tlb_size] = set_dynamic_tlb(pci_device, tlb_index, target, address, harvested_coord_translation, dynamic_tlb_ordering_modes.at(fallback_tlb));
            uint32_t transfer_size = std::min(size_in_bytes, tlb_size);
            read_block(dev, mapped_address, transfer_size, buffer_addr, m_dma_buf_size);

            size_in_bytes -= transfer_size;
            address += transfer_size;
            buffer_addr += transfer_size;
        }
        // LOG1 ("Read done Dynamic TLB with pid=%ld\n", (long)getpid());
    }
}

void tt_SiliconDevice::read_dma_buffer(
    void* mem_ptr,
    std::uint32_t address,
    std::uint16_t channel,
    std::uint32_t size_in_bytes,
    chip_id_t src_device_id) {

    log_assert(src_device_id != -1, "Must provide src_device_id for host_resident read/write");
    log_assert(channel >= 0 && channel <= g_MAX_HOST_MEM_CHANNELS, "{} - Invalid channel {} for host_resident read/write.", __FUNCTION__, channel);
    void * user_scratchspace = nullptr;

    if(hugepage_mapping.at(src_device_id).at(channel)) {
      user_scratchspace = static_cast<char*>(hugepage_mapping.at(src_device_id).at(channel)) + (address & HUGEPAGE_MAP_MASK);
    } else if (buf_mapping) {
      user_scratchspace = static_cast<char*>(buf_mapping) + (address & DMA_MAP_MASK);
    } else {
      std::string err_msg = "write_dma_buffer: Hugepage or DMAbuffer are not allocated for src_device_id: " + std::to_string(src_device_id) + " ch: " + std::to_string(channel);
      err_msg += " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)";
      throw std::runtime_error(err_msg);
    }

    LOG1("---- tt_SiliconDevice::read_dma_buffer (src_device_id: %d, ch: %d) from 0x%lx\n",  src_device_id, channel, user_scratchspace);
    
    memcpy(mem_ptr, user_scratchspace, size_in_bytes);
}

void tt_SiliconDevice::write_dma_buffer(
    const void *mem_ptr,
    std::uint32_t size,
    std::uint32_t address,
    std::uint16_t channel,
    chip_id_t src_device_id) {

    log_trace(LogSiliconDriver, "Issuing write to host side DMA Buffer inside Silicon Driver. DMA Buffer chan {} address {} size {}", channel, address, size);
    // log_debug(LogSiliconDriver, "Issuing write to host side DMA Buffer inside Silicon Driver. DMA Buffer chan {} address {} size {}", channel, address, size);

    void * user_scratchspace = nullptr;
    if(hugepage_mapping.at(src_device_id).at(channel)) {
      user_scratchspace = static_cast<char*>(hugepage_mapping.at(src_device_id).at(channel)) + (address & HUGEPAGE_MAP_MASK);
    }
    memcpy(user_scratchspace, mem_ptr, size);
}


uint32_t tt_SiliconDevice::get_power_state_arc_msg(struct PCIdevice* pci_device, tt_DevicePowerState state) {
    uint32_t msg = 0xaa00;
    switch (state) {
        case BUSY: {
            msg |= pci_device->hdev->get_architecture_implementation()->get_arc_message_arc_go_busy();
            break;
        }
        case LONG_IDLE: {
            msg |= pci_device->hdev->get_architecture_implementation()->get_arc_message_arc_go_long_idle();
            break;
        }
        case SHORT_IDLE: {
            msg |= pci_device->hdev->get_architecture_implementation()->get_arc_message_arc_go_short_idle();
            break;
        }
        default: throw std::runtime_error("Unrecognized power state.");
    }
    return msg;
}

void tt_SiliconDevice::set_pcie_power_state(tt_DevicePowerState state) {

    for (auto &device_it : m_pci_device_map){
        int d = device_it.first;
        struct PCIdevice* pci_device = device_it.second;
        uint32_t msg = get_power_state_arc_msg(pci_device, state);
        std::stringstream ss;
        ss << state;
        auto exit_code = arc_msg(d, 0xaa00 | msg, true, 0, 0);
        if (exit_code != 0) {
            throw std::runtime_error(
                "Failed to set power state to " + ss.str() + " with exit code " + std::to_string(exit_code));
        }
    }
}

int tt_SiliconDevice::get_clock(int logical_device_id) {
    uint32_t clock;
    auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(logical_device_id);
    struct PCIdevice* pci_device = get_pci_device(mmio_capable_chip_logical);
    auto exit_code = arc_msg(logical_device_id, 0xaa00 | pci_device->hdev->get_architecture_implementation()->get_arc_message_get_aiclk(), true, 0xFFFF, 0xFFFF, 1, &clock);
    if (exit_code != 0) {
        throw std::runtime_error("Failed to get aiclk value with exit code " + std::to_string(exit_code));
    }
    return clock;
}

std::map<int, int> tt_SiliconDevice::get_clocks() {
    std::map<int,int> clock_freq_map;
    for (auto &device_it : m_pci_device_map){
        int d = device_it.first;
        clock_freq_map.insert({d, get_clock(d)});
    }
    return clock_freq_map;
}

//! Simple test of communication to device/target.  true if it passes.
// bool tt_SiliconDevice::test_write_read(tt_cxy_pair target) {
//     WARN("---- tt_SiliconDevice::test_write_read not implemented\n");
//     return true;
// }

// bool tt_SiliconDevice::test_write_speed (struct PCIdevice* pci_device) {
//     TTDevice *dev = pci_device->hdev;

//     if (dev->bar0_uc == dev->bar0_wc) {
//         WARN("---- tt_SiliconDevice::test_write_speed WC not configured\n");
//     }

//     std::byte fill_value{0x42};
//     std::vector<std::byte> write_buf(architecture_implementation->get_static_tlb_size(), fill_value);

//     auto before = std::chrono::high_resolution_clock::now();
//     for (std::uint32_t y = 1; y < architecture_implementation->get_grid_size_y(); y++)
//     {
//         for (std::uint32_t x = 1; x < architecture_implementation->get_grid_size_x(); x++)
//         {
//             auto tlb_index = map_core_to_tlb(tt_xy_pair(x, y));
//             if (tlb_index < 0) { continue; }

//             auto offset = tlb_index * architecture_implementation->get_static_tlb_size();

//             memcpy(static_cast<std::byte*>(dev->bar0_wc) + offset, write_buf.data(), write_buf.size());
//         }
//     }
//     auto after = std::chrono::high_resolution_clock::now();

//     std::chrono::duration<double, std::milli> interval = after - before;

//     unsigned int write_bw = 120 * std::milli::den / interval.count();

//     LOG1("---- tt_SiliconDevice::test_write_speed Wrote 120MB @ %u MB/s\n", write_bw);

//     return (write_bw >= 512); // L1 write BW scales with AICLK, for low AICLK it will be very slow.
// }

tt_SiliconDevice::~tt_SiliconDevice () {

    LOG1 ("---- tt_SiliconDevice::~tt_SiliconDevice\n");

    for(int i = 0; i < archs_in_cluster.size(); i++) {
        if(archs_in_cluster[i] == tt::ARCH::WORMHOLE) {
            log_warning(LogSiliconDriver, "Virtual device {} for this run is Wormhole A0. This architecture is now deprecated. Please use Wormhole B0 for testing.", i);
        }
    }
    cleanup_shared_host_state();

    for (auto &device_it : m_pci_device_map){

        chip_id_t device_id = device_it.first;

        for (int ch = 0; ch < m_num_host_mem_channels; ch ++) {
            if (hugepage_mapping.at(device_id).at(ch)) {
                munmap(hugepage_mapping.at(device_id).at(ch), hugepage_mapping_size.at(device_id).at(ch));
            }
        }

        struct PCIdevice* pci_device = device_it.second;

        ttkmd_close (*pci_device);
        delete pci_device;
        pci_device = NULL;
    }
    m_pci_device_map.clear();
    ndesc.reset();
    soc_descriptor_per_chip.clear();
    dynamic_tlb_config.clear();
    tlb_config_map.clear();
    dynamic_tlb_ordering_modes.clear();
}

std::optional<std::tuple<uint32_t, uint32_t>> tt_SiliconDevice::get_tlb_data_from_target(const tt_xy_pair& target) {
    std::int32_t tlb_index = 0;
    std::optional<std::tuple<std::uint32_t, std::uint32_t>> tlb_data;

    if (tlbs_init) {
        tlb_index = map_core_to_tlb(target);
        auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
        tlb_data = architecture_implementation->describe_tlb(tlb_index);
    } 
    return tlb_data;
}

uint32_t tt_SiliconDevice::get_m_dma_buf_size() const {
    return m_dma_buf_size;
}

void tt_SiliconDevice::configure_tlb(chip_id_t logical_device_id, tt_xy_pair core, std::int32_t tlb_index, std::int32_t address, uint64_t ordering) {
    log_assert(ordering == TLB_DATA::Strict || ordering == TLB_DATA::Posted || ordering == TLB_DATA::Relaxed, "Invalid ordering specified in tt_SiliconDevice::configure_tlb");
    struct PCIdevice* pci_device = get_pci_device(logical_device_id);
    set_dynamic_tlb(pci_device, tlb_index, core, address, harvested_coord_translation, ordering);
    auto tlb_size = std::get<1>(pci_device->hdev->get_architecture_implementation()->describe_tlb(tlb_index).value());
    if(tlb_config_map.find(logical_device_id) == tlb_config_map.end()) tlb_config_map.insert({logical_device_id, {}});
    tlb_config_map[logical_device_id].insert({tlb_index, (address / tlb_size) * tlb_size});
}

void tt_SiliconDevice::set_fallback_tlb_ordering_mode(const std::string& fallback_tlb, uint64_t ordering) {
    log_assert(ordering == TLB_DATA::Strict || ordering == TLB_DATA::Posted || ordering == TLB_DATA::Relaxed, "Invalid ordering specified in tt_SiliconDevice::configure_tlb.");
    log_assert(dynamic_tlb_ordering_modes.find(fallback_tlb) != dynamic_tlb_ordering_modes.end(), "Invalid TLB specified in tt_SiliconDevice::set_fallback_tlb_ordering_mode.");
    log_assert(fallback_tlb != "LARGE_READ_TLB" &&  fallback_tlb != "LARGE_WRITE_TLB", "Ordering modes for LARGE_READ_TLB and LARGE_WRITE_TLB cannot be modified.");
    dynamic_tlb_ordering_modes.at(fallback_tlb) = ordering;
}
// This function checks that all TLBs are properly setup. It should return 0 if all is good (i.e. if init_pcie_tlb is called prior)
// int tt_SiliconDevice::test_pcie_tlb_setup (struct PCIdevice* pci_device) {
    // LOG1("---- tt_SiliconDevice::test_pcie_tlb_setup\n");
    // uint64_t tlb_data;
    // int ret_val;
    // // Check static TLBs (only active Tensix cores for GS ... Active tensix cores + ethernet cores for WH)
    // for (uint32_t y = 0; y < architecture_implementation->get_grid_size_y() - num_rows_harvested; y++) {
    //     for (uint32_t x = 0; x < architecture_implementation->get_grid_size_x(); x++) {
    //         int tlb_index = get_static_tlb_index(tt_xy_pair(x, y));
    //         auto translated_coords = harvested_coord_translation.at(pci_device -> id).at(tt_xy_pair(x, y));
    //         if (tlb_index < 0) { continue; }

    //         auto tlb_data_attempt = architecture_implementation->get_tlb_data(tlb_index, TLB_DATA {
    //             .x_end = translated_coords.x,
    //             .y_end = translated_coords.y,
    //         });
    //         if (!tlb_data_attempt.has_value()) {
    //             throw std::runtime_error("Error setting up (" + std::to_string(x) + ", " + std::to_string(y) + ") in pcie_tlb_test.");
    //         }
    //         uint64_t expected_tlb_data = tlb_data_attempt.value();

    //         uint32_t tlb_setup_addr = architecture_implementation->get_static_tlb_cfg_addr() + 8 * tlb_index; // Each tlb setup takes 2 dwords, hence 8 bytes
    //         read_regs(pci_device->hdev, tlb_setup_addr, 2, &tlb_data);

    //     }
    // }

    // // Check 16MB TLBs 1-16 for peer-to-peer communication with DRAM channel 0
    // uint64_t peer_dram_offset = architecture_implementation->get_dram_channel_0_peer2peer_region_start();
    // for (uint32_t tlb_id = 1; tlb_id < 17; tlb_id++) {
    //     auto tlb_data_expected = architecture_implementation->get_tlb_data(architecture_implementation->get_tlb_base_index_16m() + tlb_id, TLB_DATA {
    //         .local_offset = peer_dram_offset / architecture_implementation->get_dynamic_tlb_16m_size(),
    //         .x_end = architecture_implementation->get_dram_channel_0_x(),
    //         .y_end = architecture_implementation->get_dram_channel_0_y(),
    //         .ordering = TLB_DATA::Posted,
    //         .static_vc = true,
    //     });
    //     uint64_t tlb_data_observed;
    //     uint32_t tlb_setup_addr = architecture_implementation->get_dynamic_tlb_16m_cfg_addr() + 8 * tlb_id; // Each tlb setup takes 2 dwords, hence 8 bytes
    //     read_regs(pci_device->hdev, tlb_setup_addr, 2, &tlb_data_observed);
    //     ret_val = (tlb_data_expected == tlb_data_observed) ? 0 : 1;
    //     if (ret_val != 0) return ret_val;
    //     peer_dram_offset += architecture_implementation->get_dynamic_tlb_16m_size();
    // }
    // return ret_val;
//}

// Set up IATU for peer2peer
// Consider changing this function
void tt_SiliconDevice::init_pcie_iatus() {

    int starting_device_id  = m_pci_device_map.begin()->first;
    int ending_device_id    = m_pci_device_map.rbegin()->first;
    int num_enabled_devices = m_pci_device_map.size();

    LOG1("---- tt_SiliconDevice::init_pcie_iatus() num_enabled_devices: %d starting_device_id: %d ending_device_id: %d\n", num_enabled_devices, starting_device_id, ending_device_id);
    log_assert(m_num_host_mem_channels <= 1, "Maximum of 1x 1GB Host memory channels supported.");

    // Requirement for ring topology in GS, but since WH can share below code, check it again here for mmio mapped devices,
    // otherwise us/ds device calculations will not be correct. Don't expect to see this for Wormhole today.
    log_assert((starting_device_id + num_enabled_devices - 1) == ending_device_id, "The set of workload mmio-mapped target_device_id's must be sequential, without gaps.");

    for (auto &src_device_it : m_pci_device_map){
        int src_pci_id = src_device_it.first;
        struct PCIdevice* src_pci_device = src_device_it.second;

        uint32_t current_peer_region = 0;
        const int num_peer_ids = 3; // 0=HOST, 1=UPSTREAM Device, 2=DOWNSTREAM Device, 3=Unused
        for (int peer_id = 0; peer_id < num_peer_ids; peer_id++) {

            //TODO: migrate this to huge pages when that support is in
            if (peer_id == 0){
                LOG2 ("Setting up src_pci_id: %d peer_id: %d to Host. current_peer_region: %d\n", src_pci_id, peer_id, current_peer_region);
                // Device to Host (peer_id==0)
                const uint16_t host_memory_channel = 0; // Only single channel supported.
                if (hugepage_mapping.at(src_pci_id).at(host_memory_channel)) {
                    iatu_configure_peer_region(src_pci_id, current_peer_region, hugepage_physical_address.at(src_pci_id).at(host_memory_channel), HUGEPAGE_REGION_SIZE);
                    host_channel_size.insert({src_pci_device -> logical_id, {HUGEPAGE_REGION_SIZE}});
                } else if(buf_mapping) {
                    // we failed when initializing huge pages, we are using a 1MB DMA buffer as a stand-in
                    iatu_configure_peer_region(src_pci_id, current_peer_region, buf_physical_addr, DMA_BUF_REGION_SIZE);
                }
            } else if (peer_id == 1 || peer_id == 2){
                // Device to Device (peer_id==1 : Upstream, peer_id==2 : Downstream)
                // For determining upstream/downstream peers in ring topology - this matches is_target_device_downstream() in net2pipe
                int upstream_peer_device_id = src_pci_id > starting_device_id ? src_pci_id - 1 : ending_device_id;
                int downstream_peer_device_id = src_pci_id < (ending_device_id) ? src_pci_id + 1 : starting_device_id;

                int peer_device_id = peer_id == 1 ? upstream_peer_device_id : downstream_peer_device_id;

                struct PCIdevice* peer_pci_device = m_pci_device_map.at(peer_device_id);
                uint64_t peer_BAR_addr = peer_pci_device->BAR_addr;
                uint32_t peer_pci_interface_id = peer_pci_device->id;
                uint32_t TLB1_16MB_OFFSET = 0; // Was 192MB offset to DRAM, now added by net2pipe since ATU maps to base of 512MB PCI Bar.
                uint32_t PEER_REGION_SIZE = 1024 * 1024 * 1024; // Was 256MB. Want 512MB. Updated to 1024MB to match net2pipe more easily.
                // FIXME - How to reduce PEER_REGION_SIZE=256 again, and make this still work? Need to make the ATU mappings non-contiguous 256MB chunks (every 1GB?) to match net2pipe?

                LOG2 ("Setting up src_pci_id: %d peer_id: %d to Device (upstream_peer_device_id: %d downstream_peer_device_id: %d) gives peer_device_id: %d (peer_pci_interface_id: %d) current_peer_region: %d\n",
                    src_pci_id, peer_id, upstream_peer_device_id, downstream_peer_device_id, peer_device_id, peer_pci_interface_id, current_peer_region );

                iatu_configure_peer_region (src_pci_id, current_peer_region, peer_BAR_addr + TLB1_16MB_OFFSET, PEER_REGION_SIZE);
            }
            current_peer_region ++;
        }
    }
}

// TT<->TT P2P support removed in favor of increased Host memory.
void tt_SiliconDevice::init_pcie_iatus_no_p2p() {

    int num_enabled_devices = m_pci_device_map.size();
    LOG1("---- tt_SiliconDevice::init_pcie_iatus_no_p2p() num_enabled_devices: %d\n", num_enabled_devices);
    log_assert(m_num_host_mem_channels <= g_MAX_HOST_MEM_CHANNELS, "Maximum of {} 1GB Host memory channels supported.",  g_MAX_HOST_MEM_CHANNELS);

    for (auto &src_device_it : m_pci_device_map){
        int src_pci_id = src_device_it.first;
        struct PCIdevice* src_pci_device = src_device_it.second;

        // Device to Host (multiple channels)
        for (int channel_id = 0; channel_id < m_num_host_mem_channels; channel_id++) {
            // TODO - Try to remove DMA buffer support.
            if (hugepage_mapping.at(src_pci_id).at(channel_id)) {
                std::uint32_t region_size = HUGEPAGE_REGION_SIZE;
                if(channel_id == 3) region_size = 805306368; // Remove 256MB from full 1GB for channel 3 (iATU limitation)
                iatu_configure_peer_region(src_pci_id, channel_id, hugepage_physical_address.at(src_pci_id).at(channel_id), region_size);
                if(host_channel_size.find(src_pci_device -> logical_id) == host_channel_size.end()) {
                     host_channel_size.insert({src_pci_device -> logical_id, {}});
                }
                host_channel_size.at(src_pci_device -> logical_id).push_back(region_size);
            } else if(buf_mapping) {
                // we failed when initializing huge pages, we are using a 1MB DMA buffer as a stand-in
                iatu_configure_peer_region(src_pci_id, channel_id, buf_physical_addr, DMA_BUF_REGION_SIZE);
            }
        }
    }
}

uint32_t tt_SiliconDevice::dma_allocation_size(chip_id_t src_device_id)
{

  // Fall back to first device if no src_device_id is provided. Assumes all devices have the same size, which is true.
  chip_id_t device_index = src_device_id == -1 ? m_pci_device_map.begin()->first : src_device_id;

  if (hugepage_mapping.at(device_index).at(0)) {
    return HUGEPAGE_REGION_SIZE;
  } else if (buf_mapping) {
    return DMA_BUF_REGION_SIZE;
  } else {
    log_fatal("Nothing has been allocated yet");
    return 0;
  }
}




// Looks for hugetlbfs inside /proc/mounts matching desired pagesize (typically 1G)
std::string find_hugepage_dir(std::size_t pagesize)
{

    static const std::regex hugetlbfs_mount_re("^(nodev|hugetlbfs) (" + hugepage_dir + ") hugetlbfs ([^ ]+) 0 0$");
    static const std::regex pagesize_re("(?:^|,)pagesize=([0-9]+)([KMGT])(?:,|$)");

    std::ifstream proc_mounts("/proc/mounts");

    for (std::string line; std::getline(proc_mounts, line); )
    {
        if (std::smatch mount_match; std::regex_match(line, mount_match, hugetlbfs_mount_re))
        {
            std::string options = mount_match[3];
            if (std::smatch pagesize_match; std::regex_search(options, pagesize_match, pagesize_re))
            {
                std::size_t mount_page_size = std::stoull(pagesize_match[1]);
                switch (pagesize_match[2].str()[0])
                {
                    case 'T': mount_page_size <<= 10;
                    case 'G': mount_page_size <<= 10;
                    case 'M': mount_page_size <<= 10;
                    case 'K': mount_page_size <<= 10;
                }

                if (mount_page_size == pagesize)
                {
                    return mount_match[2];
                }
            }
        }
    }

    WARN("---- ttSiliconDevice::find_hugepage_dir: no huge page mount found in /proc/mounts for path: %s with hugepage_size: %d.\n", hugepage_dir.c_str(), pagesize);
    return std::string();
}

// Open a file in <hugepage_dir> for the hugepage mapping.
// All processes operating on the same pipeline must agree on the file name.
// Today we assume there's only one pipeline running within the system.
// One hugepage per device such that each device gets unique memory.
int tt_SiliconDevice::open_hugepage_file(const std::string &dir, chip_id_t physical_device_id, uint16_t channel) {
    std::vector<char> filename;
    static const char pipeline_name[] = "tenstorrent";

    filename.insert(filename.end(), dir.begin(), dir.end());
    if (filename.back() != '/') filename.push_back('/');

    // In order to limit number of hugepages while transition from shared hugepage (1 per system) to unique
    // hugepage per device, will share original/shared hugepage filename with physical device 0.
    if (physical_device_id != 0 || channel != 0){
        std::string device_id_str = "device_" + std::to_string((int)physical_device_id) + "_";
        filename.insert(filename.end(), device_id_str.begin(), device_id_str.end());
    }

    if (channel != 0) {
        std::string channel_id_str = "channel_" + std::to_string(channel) + "_";
        filename.insert(filename.end(), channel_id_str.begin(), channel_id_str.end());
    }

    filename.insert(filename.end(), std::begin(pipeline_name), std::end(pipeline_name)); // includes NUL terminator

    std::string filename_str(filename.begin(), filename.end());
    filename_str.erase(std::find(filename_str.begin(), filename_str.end(), '\0'), filename_str.end()); // Erase NULL terminator for printing.
    LOG1("---- ttSiliconDevice::open_hugepage_file: using filename: %s for physical_device_id: %d channel: %d\n", filename_str.c_str(), physical_device_id, channel);

    // Save original and set umask to unrestricted.
    auto old_umask = umask(0);

    int fd = open(filename.data(), O_RDWR | O_CREAT | O_CLOEXEC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IWOTH | S_IROTH );
    if (fd == -1 && errno == EACCES) {
        WARN("---- ttSiliconDevice::open_hugepage_file could not open filename: %s on first try, unlinking it and retrying.\n", filename_str.c_str());
        unlink(filename.data());
        fd = open(filename.data(), O_RDWR | O_CREAT | O_CLOEXEC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IWOTH | S_IROTH );
    }

    // Restore original mask
    umask(old_umask);

    if (fd == -1) {
        WARN("---- open_hugepage_file failed\n");
        return -1;
    }

    return fd;
}

bool tt_SiliconDevice::init_dmabuf(chip_id_t device_id) {
    if (buf_mapping == nullptr) {

        TTDevice *dev = m_pci_device_map.begin()->second->hdev;

        DMAbuffer buf = pci_allocate_dma_buffer(dev, DMA_BUF_REGION_SIZE);
        buf_mapping = static_cast<void*>(reinterpret_cast<uint32_t*>(pci_dma_buffer_get_user_addr(buf)));
        buf_physical_addr= pci_dma_buffer_get_physical_addr(buf);
    }
    return true;
}

bool tt_SiliconDevice::init_dma_turbo_buf (struct PCIdevice* pci_device) {
    // Allocate buffers for DMA transfer data and flag
    pci_device->hdev->dma_completion_flag_buffer = pci_allocate_dma_buffer(pci_device->hdev, sizeof(uint64_t));
    pci_device->hdev->dma_transfer_buffer = pci_allocate_dma_buffer(pci_device->hdev, m_dma_buf_size);
    pcie_init_dma_transfer_turbo(pci_device);
    return true;
}

bool tt_SiliconDevice::uninit_dma_turbo_buf (struct PCIdevice* pci_device) {
    struct DMAbuffer &flag_buffer = pci_device->hdev->dma_completion_flag_buffer;
    struct DMAbuffer &xfer_buffer = pci_device->hdev->dma_transfer_buffer;
    if (flag_buffer.pBuf) {
        for (auto it = pci_device->hdev->dma_buffer_mappings.begin(); it != pci_device->hdev->dma_buffer_mappings.end();) {
            if (it->pBuf == flag_buffer.pBuf) {
                it = pci_device->hdev->dma_buffer_mappings.erase(it);
            } else {
                ++it;
            }
        }
        munmap(flag_buffer.pBuf, flag_buffer.size);
    }
    if (xfer_buffer.pBuf) {
        for (auto it = pci_device->hdev->dma_buffer_mappings.begin(); it != pci_device->hdev->dma_buffer_mappings.end();) {
            if (it->pBuf == xfer_buffer.pBuf) {
                it = pci_device->hdev->dma_buffer_mappings.erase(it);
            } else {
                ++it;
            }
        }
        munmap(xfer_buffer.pBuf, xfer_buffer.size);
    }
    return true;
}

// For debug purposes when various stages fails.
void print_file_contents(std::string filename, std::string hint = ""){
    if (std::filesystem::exists(filename)){
        std::ifstream meminfo(filename);
        if (meminfo.is_open()){
            std::cout << std::endl << "File " << filename << " " << hint << " is: " << std::endl;
            std::cout << meminfo.rdbuf();
        }
    }
}

// Initialize hugepage, N per device (all same size).
bool tt_SiliconDevice::init_hugepage(chip_id_t device_id) {
    const std::size_t hugepage_size = (std::size_t)1 << 30;
    const std::size_t mapping_size = (std::size_t) HUGEPAGE_REGION_SIZE;

    // Convert from logical (device_id in netlist) to physical device_id (in case of virtualization)
    auto physical_device_id = m_pci_device_map.at(device_id)->id;

    std::string hugepage_dir = find_hugepage_dir(hugepage_size);
    if (hugepage_dir.empty()) {
        WARN("---- ttSiliconDevice::init_hugepage: no huge page mount found for hugepage_size: %d.\n", hugepage_size);
        return false;
    }

    bool success = true;

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < m_num_host_mem_channels; ch++) {

        int hugepage_fd = open_hugepage_file(hugepage_dir, physical_device_id, ch);
        if (hugepage_fd == -1) {
            // Probably a permissions problem.
            WARN("---- ttSiliconDevice::init_hugepage: physical_device_id: %d ch: %d creating hugepage mapping file failed.\n", physical_device_id, ch);
            success = false;
            continue;
        }

        std::byte *mapping = static_cast<std::byte*>(mmap(nullptr, mapping_size, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_POPULATE, hugepage_fd, 0));

        close(hugepage_fd);

        if (mapping == MAP_FAILED) {
            uint32_t num_tt_mmio_devices_for_arch = tt::cpuset::tt_cpuset_allocator::get_num_tt_pci_devices_by_pci_device_id(m_pci_device_map.at(device_id)->device_id, m_pci_device_map.at(device_id)->revision_id);
            WARN("---- ttSiliconDevice::init_hugepage: physical_device_id: %d ch: %d mapping hugepage failed. (errno: %s).\n", physical_device_id, ch, strerror(errno));
            WARN("---- Possible hint: /proc/cmdline should have hugepages=N, nr_hugepages=N - (N = NUM_MMIO_TT_DEVICES * (is_grayskull ? 1 : 4). NUM_MMIO_DEVICES = %d\n", num_tt_mmio_devices_for_arch);
            print_file_contents("/proc/cmdline");\
            print_file_contents("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages"); // Hardcoded for 1GB hugepage.
            success = false;
            continue;
        }

        // Beter performance if hugepage just allocated (populate flag to prevent lazy alloc) is migrated to same numanode as TT device.
        if (!tt::cpuset::tt_cpuset_allocator::bind_area_to_memory_nodeset(physical_device_id, mapping, mapping_size)){
            WARN("---- ttSiliconDevice::init_hugepage: bind_area_to_memory_nodeset() failed (physical_device_id: %d ch: %d). "
            "Hugepage allocation is not on NumaNode matching TT Device. Side-Effect is decreased Device->Host perf (Issue #893).\n",
            physical_device_id, ch);
        }

        tenstorrent_pin_pages pin_pages;
        memset(&pin_pages, 0, sizeof(pin_pages));
        pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
        pin_pages.in.flags = TENSTORRENT_PIN_PAGES_CONTIGUOUS;
        pin_pages.in.virtual_address = reinterpret_cast<std::uintptr_t>(mapping);
        pin_pages.in.size = mapping_size;

        auto &fd = g_SINGLE_PIN_PAGE_PER_FD_WORKAROND ? m_pci_device_map.at(device_id)->hdev->device_fd_per_host_ch[ch] : m_pci_device_map.at(device_id)->hdev->device_fd;

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) == -1) {
            WARN("---- ttSiliconDevice::init_hugepage: physical_device_id: %d ch: %d TENSTORRENT_IOCTL_PIN_PAGES failed (errno: %s). Common Issue: Requires TTMKD >= 1.11, see following file contents...\n", physical_device_id, ch, strerror(errno));
            munmap(mapping, mapping_size);
            print_file_contents("/sys/module/tenstorrent/version", "(TTKMD version)");
            print_file_contents("/proc/meminfo");
            print_file_contents("/proc/buddyinfo");
            success = false;
            continue;
        }

        hugepage_mapping.at(device_id).at(ch) = mapping;
        hugepage_mapping_size.at(device_id).at(ch) = mapping_size;
        hugepage_physical_address.at(device_id).at(ch) = pin_pages.out.physical_address;

        LOG1("---- ttSiliconDevice::init_hugepage: physical_device_id: %d ch: %d mapping_size: %d physical address 0x%llx\n", physical_device_id, ch, mapping_size, (unsigned long long)hugepage_physical_address.at(device_id).at(ch));

    }

    return success;
}

int tt_SiliconDevice::test_setup_interface () {
    if (arch_name == tt::ARCH::GRAYSKULL) {
        int ret_val = 0;
        TTDevice *dev = m_pci_device_map.begin()->second->hdev;

        uint32_t mapped_reg = set_dynamic_tlb(m_pci_device_map.begin()->second, dev->get_architecture_implementation()->get_reg_tlb(), tt_xy_pair(0, 0), 0xffb20108, harvested_coord_translation).bar_offset;

        uint32_t regval = 0;
        read_regs(dev, mapped_reg, 1, &regval);
        ret_val = (regval != 0xffffffff && ((regval & 0x1) == 1)) ? 0 : 1;
        return ret_val;
    }
    else if (arch_name == tt::ARCH::WORMHOLE || arch_name == tt::ARCH::WORMHOLE_B0) {
        int ret_val = 0;
        TTDevice *dev = m_pci_device_map.begin()->second->hdev;

        uint32_t mapped_reg = set_dynamic_tlb(m_pci_device_map.begin()->second, dev->get_architecture_implementation()->get_reg_tlb(), tt_xy_pair(1, 0), 0xffb20108, harvested_coord_translation).bar_offset;

        uint32_t regval = 0;
        read_regs(dev, mapped_reg, 1, &regval);
        ret_val = (regval != 0xffffffff && (regval == 33)) ? 0 : 1;
        return ret_val;
    }
    else if (arch_name == tt::ARCH::BLACKHOLE) {
        // MT Inital BH - Try to enable this, but double check "regval == 33"
        // int ret_val = 0;
        // TTDevice *dev = m_pci_device_map.begin()->second->hdev;

        // uint32_t mapped_reg = set_dynamic_tlb(m_pci_device_map.begin()->second, dev->get_architecture_implementation()->get_reg_tlb(), tt_xy_pair(1, 0), 0xffb20108, harvested_coord_translation).bar_offset;

        // uint32_t regval = 0;
        // read_regs(dev, mapped_reg, 1, &regval);
        // ret_val = (regval != 0xffffffff && (regval == 33)) ? 0 : 1;
        // return ret_val;
        return 0;
    }
    else {
        throw std::runtime_error("Unsupported architecture: " + get_arch_str(arch_name));
    }
}

// Code used to test non existent broadcast TLB
// Keep for now, in case we need to test broadcast TLB again.
// int tt_SiliconDevice::test_broadcast (int logical_device_id) {
//     LOG1("---- tt_SiliconDevice::test_broadcast\n");

//     int ret_val = 0;
//     struct PCIdevice* pci_device = get_pci_device(logical_device_id);

//     assert (test_pcie_tlb_setup(pci_device) == 0);

//     std::vector<std::uint32_t> fill_array (1024, 0);
//     uint32_t broadcast_bar_offset = architecture_implementation->get_broadcast_tlb_index() * architecture_implementation->get_static_tlb_size();
//     LOG2 ("broadcast_bar_offset = 0x%x\n", broadcast_bar_offset);

//     uint64_t fill_array_ptr = (uint64_t)(&fill_array[0]);

//     // a. Fill with increasing numbers
//     //
//     for (size_t i = 0; i < fill_array.size(); i++) {
//         fill_array[i] = i;
//     }
//     write_block(pci_device->hdev, broadcast_bar_offset, fill_array.size() * sizeof (std::uint32_t), fill_array_ptr, m_dma_buf_size);

//     // Check individual locations
//     for (uint32_t xi = 0; xi < architecture_implementation->get_t6_x_locations().size(); xi++) {
//         for (uint32_t yi = 0; yi < architecture_implementation->get_t6_y_locations().size(); yi++) {
//             tt_cxy_pair read_loc(logical_device_id, architecture_implementation->get_t6_x_locations()[xi], architecture_implementation->get_t6_y_locations()[yi]);
//             read_vector (fill_array, read_loc, 0, fill_array.size() * sizeof (fill_array[0]) );
//             for (size_t i = 0; i < fill_array.size(); i++) {
//                 ret_val = (fill_array[i] == i) ? 0 : 1;
//                 if (ret_val) return ret_val;
//             }
//         }
//     }

//     // b. Test with zeroes
//     //
//     std::vector<std::uint32_t> fill_array_zeroes (1024, 0);
//     uint64_t fill_array_zeroes_ptr = (uint64_t)(&fill_array_zeroes[0]);
//     write_block(pci_device->hdev, broadcast_bar_offset, fill_array.size() * sizeof (std::uint32_t), fill_array_zeroes_ptr, m_dma_buf_size);

//     // Check individual locations
//     for (uint32_t xi = 0; xi < architecture_implementation->get_t6_x_locations().size(); xi++) {
//         for (uint32_t yi = 0; yi < architecture_implementation->get_t6_y_locations().size(); yi++) {
//             tt_cxy_pair read_loc(logical_device_id, architecture_implementation->get_t6_x_locations()[xi], architecture_implementation->get_t6_y_locations()[yi]);
//             read_vector (fill_array, read_loc, 0, fill_array.size() * sizeof (fill_array_zeroes[0]) );
//             for (size_t i = 0; i < fill_array.size(); i++) {
//                 ret_val = (fill_array_zeroes[i] == 0) ? 0 : 1;
//                 if (ret_val) return ret_val;
//             }
//         }
//     }

//     return ret_val;
// }

void tt_SiliconDevice::bar_write32 (int logical_device_id, uint32_t addr, uint32_t data) {
    TTDevice* dev = get_pci_device(logical_device_id)->hdev;

    if (addr < dev->bar0_uc_offset) {
        write_block (dev, addr, sizeof(data), reinterpret_cast<const uint8_t*>(&data), m_dma_buf_size);
    } else {
        write_regs (dev, addr, 1, &data);
    }
}

uint32_t tt_SiliconDevice::bar_read32 (int logical_device_id, uint32_t addr) {
    TTDevice* dev = get_pci_device(logical_device_id)->hdev;

    uint32_t data;
    if (addr < dev->bar0_uc_offset) {
        read_block (dev, addr, sizeof(data), reinterpret_cast<uint8_t*>(&data), m_dma_buf_size);
    } else {
        read_regs (dev, addr, 1, &data);
    }
    return data;
}

// Returns 0 if everything was OK
int tt_SiliconDevice::pcie_arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done, uint32_t arg0, uint32_t arg1, int timeout, uint32_t *return_3, uint32_t *return_4) {


    if ((msg_code & 0xff00) != 0xaa00) {
        ERROR ("Malformed message. msg_code is 0x%x but should be 0xaa..\n", msg_code);
    }
    log_assert(arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args"); // Only 16 bits are allowed

    struct PCIdevice* pci_device = get_pci_device(logical_device_id);
    auto architecture_implementation = pci_device->hdev->get_architecture_implementation();

    // Exclusive access for a single process at a time. Based on physical pci interface id.
    std::string msg_type = "ARC_MSG";
    const scoped_lock<named_mutex> lock(*get_mutex(msg_type, pci_device->id));
    uint32_t fw_arg = arg0 | (arg1<<16);
    int exit_code = 0;

    bar_write32 (logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4, fw_arg);
    bar_write32 (logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 5 * 4, msg_code);

    uint32_t misc = bar_read32 (logical_device_id, architecture_implementation->get_arc_reset_arc_misc_cntl_offset());
    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", logical_device_id);
        return 1;
    } else {
        bar_write32(logical_device_id, architecture_implementation->get_arc_reset_arc_misc_cntl_offset(), misc | (1 << 16));
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto timeout_seconds = std::chrono::seconds(timeout);
        auto start = std::chrono::system_clock::now();
        while (true) {
            if (std::chrono::system_clock::now() - start > timeout_seconds) {
                throw std::runtime_error("Timed out after waiting " + std::to_string(timeout) + " seconds for device " + std::to_string(logical_device_id) + " ARC to respond");
            }

            status = bar_read32(logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 5 * 4);

            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    *return_3 = bar_read32(logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4);
                }

                if (return_4 != nullptr) {
                    *return_4 = bar_read32(logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 4 * 4);
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == MSG_ERROR_REPLY) {
                log_warning(LogSiliconDriver, "On device {}, message code 0x{:x} not recognized by FW", logical_device_id, msg_code);
                exit_code = MSG_ERROR_REPLY;
                break;
            }
        }
    }

    detect_ffffffff_read(pci_device->hdev);
    return exit_code;
}

int tt_SiliconDevice::iatu_configure_peer_region (int logical_device_id, uint32_t peer_region_id, uint64_t bar_addr_64, uint32_t region_size) {
    // utility.INFO (f"    Setting peer_region_id {peer_region_id} to BAR addr 0x%x" % bar_addr_64)
    uint32_t dest_bar_lo = bar_addr_64 & 0xffffffff;
    uint32_t dest_bar_hi = (bar_addr_64 >> 32) & 0xffffffff;
    std::uint32_t region_id_to_use = peer_region_id;
    if(peer_region_id == 3) region_id_to_use = 4; // Hack use region 4 for channel 3..this ensures that we have a smaller chan 3 address space with the correct start offset
    struct PCIdevice* pci_device = get_pci_device(logical_device_id);
    auto architecture_implementation = pci_device->hdev->get_architecture_implementation();

    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 0 * 4, region_id_to_use);
    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 1 * 4, dest_bar_lo);
    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 2 * 4, dest_bar_hi);
    bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 3 * 4, region_size);
    arc_msg(logical_device_id, 0xaa00 | architecture_implementation->get_arc_message_setup_iatu_for_peer_to_peer(), true, 0, 0);

    // Print what just happened
    uint32_t peer_region_start = region_id_to_use*region_size;
    uint32_t peer_region_end = (region_id_to_use+1)*region_size - 1;
    LOG1 ("    [region id %d] NOC to PCI address range 0x%x-0x%x mapped to addr 0x%llx\n", peer_region_id, peer_region_start, peer_region_end, bar_addr_64);
    return 0;
}

// Returns broken rows as bits set to 1 in 'memory' and 'logic'
uint32_t tt_SiliconDevice::get_harvested_noc_rows(uint32_t harvesting_mask) {
    auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
    const std::vector<uint32_t> &harv_to_noc_loc = architecture_implementation->get_harvesting_noc_locations();
    uint32_t harv_noc_rows = 0;
    std::string harv_noc_rows_str = "";

    for (int pos=0; pos<harv_to_noc_loc.size(); ++pos) {
        bool is_row_harvested = harvesting_mask & 0x1;
        if (is_row_harvested) {
            harv_noc_rows |= (1 << harv_to_noc_loc[pos]);
            if (harv_noc_rows_str != "") harv_noc_rows_str += ", ";
            harv_noc_rows_str += std::to_string(harv_to_noc_loc[pos]);
        }
        harvesting_mask = harvesting_mask >> 1;
    }
    if (harv_noc_rows > 0) {
        LOG1 ("HARVESTING NOC Y-LOC 0x%x = {%s}\n", harv_noc_rows, harv_noc_rows_str.c_str());
    }
    return harv_noc_rows;
}

uint32_t tt_SiliconDevice::get_harvested_rows (int logical_device_id) {
    const char* harv_override = std::getenv("T6PY_HARVESTING_OVERRIDE");
    uint32_t harv = 0xffffffff;
    if (harv_override) {
        harv = std::stoul(harv_override, nullptr, 16);
    } else {
        auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(logical_device_id);
        struct PCIdevice* pci_device = get_pci_device(mmio_capable_chip_logical);
        int harvesting_msg_code = arc_msg(logical_device_id, 0xaa00 | pci_device->hdev->get_architecture_implementation()->get_arc_message_arc_get_harvesting(), true, 0, 0, 1, &harv);
        log_assert(harvesting_msg_code != MSG_ERROR_REPLY, "Failed to read harvested rows from device {}", logical_device_id);
    }
    log_assert(harv != 0xffffffff, "Readback 0xffffffff for harvesting info. Chip is fused incorrectly!");
    LOG1("HARVESTING %s, 0x%x\n", (harv==0) ? "DISABLED":"ENABLED", harv);
    
    uint32_t memory = harv & 0x3ff;
    uint32_t logic = (harv >> 10) & 0x3ff;
    return (memory|logic);
}

uint32_t tt_SiliconDevice::get_harvested_noc_rows_for_chip (int logical_device_id) {
    return get_harvested_noc_rows(get_harvested_rows(logical_device_id));
}

void tt_SiliconDevice::enable_local_ethernet_queue(const chip_id_t &device_id, int timeout) {
    uint32_t msg_success = 0x0;
    auto timeout_seconds = std::chrono::seconds(timeout);
    auto start = std::chrono::system_clock::now();
    while (msg_success != 1) {
        if (std::chrono::system_clock::now() - start > timeout_seconds) {
            throw std::runtime_error("Timed out after waiting " + std::to_string(timeout) + " seconds for DRAM to finish training");
        }

        if (arc_msg(device_id, 0xaa58, true, 0xFFFF, 0xFFFF, 1, &msg_success) == MSG_ERROR_REPLY) {
            break;
        }
    }
}


void *tt_SiliconDevice::channel_0_address(std::uint32_t offset, std::uint32_t device_id) const {
    // This hard-codes that we use 16MB TLB #1 onwards for the mapping. See tt_SiliconDevice::init_pcie_tlb.
    log_assert(ndesc->is_chip_mmio_capable(device_id), "Cannot call channel_0_address for non-MMIO device");
    struct PCIdevice* pci_device = get_pci_device(device_id);
    auto architecture_implementation = pci_device->hdev->get_architecture_implementation();
    std::uint64_t bar0_offset = offset - architecture_implementation->get_dram_channel_0_peer2peer_region_start()
                                + architecture_implementation->get_dynamic_tlb_16m_base() + architecture_implementation->get_dynamic_tlb_16m_size();

    return static_cast<std::byte*>(pci_device->hdev->bar0_wc) + bar0_offset;
}

void *tt_SiliconDevice::host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {

    if (hugepage_mapping.at(src_device_id).at(channel) != nullptr) {
        return static_cast<std::byte*>(hugepage_mapping.at(src_device_id).at(channel)) + offset;
    } else {
        return nullptr;
    }
}

// Wrapper for throwing more helpful exception when not-enabled pci intf is accessed.
inline struct PCIdevice* tt_SiliconDevice::get_pci_device(int device_id) const {
    if (!m_pci_device_map.count(device_id)){
        throw std::runtime_error(std::string("device_id: " + std::to_string(device_id) + " attempted to be accessed, but is not enabled."));
    }
    return m_pci_device_map.at(device_id);
}

std::shared_ptr<boost::interprocess::named_mutex> tt_SiliconDevice::get_mutex(const std::string& tlb_name, int pci_interface_id) {
    std::string mutex_name = tlb_name + std::to_string(pci_interface_id);
    return hardware_resource_mutex_map.at(mutex_name);
}


std::unordered_map<chip_id_t, chip_id_t> tt_SiliconDevice::get_logical_to_physical_mmio_device_id_map(std::vector<chip_id_t> physical_device_ids){

    std::unordered_map<chip_id_t, chip_id_t> logical_to_physical_mmio_device_id_map;

    LOG1("get_logical_to_physical_mmio_device_id_map() -- num_physical_devices: %d\n", physical_device_ids.size());

    for (int logical_device_idx=0; logical_device_idx < physical_device_ids.size(); logical_device_idx++){
        logical_to_physical_mmio_device_id_map.insert({logical_device_idx, physical_device_ids.at(logical_device_idx)});
    }

    return logical_to_physical_mmio_device_id_map;

}


// Get PCI bus_id info for looking up TT devices in hwloc to find associated CPU package.
std::map<chip_id_t, std::string> tt_SiliconDevice::get_physical_device_id_to_bus_id_map(std::vector<chip_id_t> physical_device_ids){

    std::map<int, std::string> physical_device_id_to_bus_id_map;

    for (auto &pci_interface_id : physical_device_ids){

        auto ttdev = std::make_unique<TTDevice>(TTDevice::open(pci_interface_id));

        std::ostringstream pci_bsf;
        pci_bsf << std::hex << std::setw(2) << std::setfill('0') << (int) ttdev->pci_bus << ":";
        pci_bsf << std::hex << std::setw(2) << std::setfill('0') << (int) ttdev->pci_device << ".";
        pci_bsf << std::hex << (int) ttdev->pci_function;

        std::string pci_bsf_str = pci_bsf.str();
        LOG2("get_physical_device_id_to_bus_id_map() -- pci_interface_id: %d BSF: %s\n", pci_interface_id, pci_bsf_str.c_str());
        physical_device_id_to_bus_id_map.insert({pci_interface_id, pci_bsf_str});

    }

    return physical_device_id_to_bus_id_map;

}

uint64_t tt_SiliconDevice::get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint64_t offset) {
    uint64_t result = chip_y;
    uint64_t noc_addr_local_bits_mask = (1UL << eth_interface_params.NOC_ADDR_LOCAL_BITS) - 1;
    result <<= eth_interface_params.NOC_ADDR_NODE_ID_BITS;
    result |= chip_x;
    result <<= eth_interface_params.NOC_ADDR_NODE_ID_BITS;
    result |= noc_y;
    result <<= eth_interface_params.NOC_ADDR_NODE_ID_BITS;
    result |= noc_x;
    result <<= eth_interface_params.NOC_ADDR_LOCAL_BITS;
    result |= (noc_addr_local_bits_mask & offset);
    return result;
}

uint16_t tt_SiliconDevice::get_sys_rack(uint32_t rack_x, uint32_t rack_y) {
    uint32_t result = rack_y;
    result <<= eth_interface_params.ETH_RACK_COORD_WIDTH;
    result |= rack_x;

    return result;
}

bool tt_SiliconDevice::is_non_mmio_cmd_q_full(uint32_t curr_wptr, uint32_t curr_rptr) {
  return (curr_wptr != curr_rptr) && ((curr_wptr & eth_interface_params.CMD_BUF_SIZE_MASK) == (curr_rptr & eth_interface_params.CMD_BUF_SIZE_MASK));
}

/*
 *
 *                                       NON_MMIO_MUTEX Usage
 *
 * Relevant functions:
 *  - write_to_non_mmio_device
 *  - rolled_write_to_non_mmio_device
 *  - read_from_non_mmio_device
 *
 * The non-MMIO read/write functions (excluding the `*_epoch_cmd` variants) are responsible for the
 * writes/reads to/from those wormhole chips that aren't memory mapped or directly host connected.
 * To get the data to or from those other chips, there is a memory transfer protocol - initiated on
 * the host side but carried out by any number of the ethernet cores (the ethernet core pool is dictated
 * by `this->NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS`) on the MMIO chips (e.g. typically just the one chip in a galaxy).
 *
 * There is a command queue structure in ethernet core FW to accept these read/write commands. However, there is no
 * atomic increment (from host side) for the write pointers of these queues, nor is there any sort of other hardware
 * mutual exclusion (as of WH) from host side when populating commands into the queue (as in when the host pushes a
 * write command into the ethernet core's queue).
 *
 * Therefore, any of these non_mmio commands from host side need to be synchronized so they don't accidentally corrupt
 * each other. The finest granularity possible to synchronize on would be the command slot and wrptr (per core),
 * but wrptr updates also need to be coordinated:
 *  - you can't increment wrptr unless you are writing to the next index and your write is complete
 *  - if two threads could guarantee separate command slots, they'd need to order their wrptr updates from lowest to
 *    highest and based on completion of command writes.
 *
 * Stepping back a little bit, a sort of interprocess synchronization is required because the driver may be invoked
 * from several processes. Indeed from pybuda (python), we'd typically needs to spin up multiple processes:
 *   - 1 for pushing inputs
 *   - 1 for popping outputs
 *   - 1 for managing execution state
 *  (or some variation along those lines).
 *
 * The interprocess mutex from measurements takes a while. While not seconds, it's non-trivial such that locking and
 * unlocking at fine granularity would be more detrimental to performance than acquiring it for a large block.
 *
 * Considering the above, the current chosen approach is to make each of these calls acquired a shared mutex:
 * `NON_MMIO_MUTEX_NAME`
 *  - They acquire at a relatively large granularity -> for the entire duration of the function where we interact
 *    with the ethernet core (read/write) and where we use `active_core` to choose a core.
 *    - Simplifies synchronization while we reach stability
 *  - We need to include any usage (read/modify) of `active_core` in the mutex acquisition scope.
 *
 * Other schemes may be more performant.
 */


/*
 * Note that this function is required to acquire the `NON_MMIO_MUTEX_NAME` mutex for interacting with the
 * ethernet core (host) command queue DO NOT issue any pcie reads/writes to the ethernet core prior to acquiring the
 * mutex. For extra information, see the "NON_MMIO_MUTEX Usage" above
 */


void tt_SiliconDevice::write_to_non_mmio_device(
                        const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t address, 
                        bool broadcast, std::vector<int> broadcast_header) {
    
    chip_id_t mmio_capable_chip_logical;
    
    if(broadcast) {
        mmio_capable_chip_logical = core.chip;
    }
    else {
        mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(core.chip);
    }

    if (non_mmio_transfer_cores_customized) {
        log_assert(active_eth_core_idx_per_chip.find(mmio_capable_chip_logical) != active_eth_core_idx_per_chip.end(), "Ethernet Cores for Host to Cluster communication were not initialized for all MMIO devices.");
    }

    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);
    constexpr int BROADCAST_HEADER_SIZE = sizeof(data_word_t) * 8; // Broadcast header is 8 words
    const auto target_chip = ndesc->get_chip_locations().at(core.chip);

    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";
    translate_to_noc_table_coords(*this->get_target_mmio_device_ids().begin(), core.y, core.x);
    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_q_ptrs = std::vector<uint32_t>(eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2 / sizeof(uint32_t));

    std::vector<std::uint32_t> data_block;

    routing_cmd_t *new_cmd;

    uint32_t buffer_id = 0;
    uint32_t timestamp = 0; //CMD_TIMESTAMP;
    bool use_dram;
    uint32_t max_block_size;

    flush_non_mmio = true;
    // Broadcast requires block writes to host dram
    use_dram = broadcast || (size_in_bytes > 256 * DATA_WORD_SIZE);
    max_block_size = use_dram ? host_address_params.ETH_ROUTING_BLOCK_SIZE : eth_interface_params.MAX_BLOCK_SIZE;

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    const scoped_lock<named_mutex> lock(
        *get_mutex(NON_MMIO_MUTEX_NAME, this->get_pci_device(mmio_capable_chip_logical)->id));

    int& active_core_for_txn = non_mmio_transfer_cores_customized ? active_eth_core_idx_per_chip.at(mmio_capable_chip_logical) : active_core;
    tt_cxy_pair remote_transfer_ethernet_core = remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn];

    erisc_command.resize(sizeof(routing_cmd_t)/DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t *)&erisc_command[0];
    read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
    uint32_t full_count = 0;
    uint32_t offset = 0;
    uint32_t block_size;

    bool full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];
    while (offset < size_in_bytes) {
        while (full) {
            read_device_memory(erisc_q_rptr.data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES + eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES, DATA_WORD_SIZE, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0],erisc_q_rptr[0]);
            full_count++;
        }
        //full = true;
        // set full only if this command will make the q full.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.
        //full = is_non_mmio_cmd_q_full((erisc_q_ptrs[0] + 1) & CMD_BUF_PTR_MASK, erisc_q_rptr[0]);

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.CMD_BUF_SIZE_MASK;
        if ((address + offset) & 0x1F) { // address not 32-byte aligned
            block_size = DATA_WORD_SIZE; // 4 byte aligned
        } else {
            // For broadcast we prepend a 32byte header. Decrease block size (size of payload) by this amount.
            block_size = offset + max_block_size > size_in_bytes + 32 * broadcast ? size_in_bytes - offset : max_block_size - 32 * broadcast;
            // Explictly align block_size to 4 bytes, in case the input buffer is not uint32_t aligned
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;
        }
        // For 4 byte aligned data, transfer_size always == block_size. For unaligned data, transfer_size < block_size in the last block
        uint32_t transfer_size = std::min(block_size, size_in_bytes - offset); // Host side data size that needs to be copied
        // Use block mode for broadcast
        uint32_t req_flags = (broadcast || (block_size > DATA_WORD_SIZE)) ? (eth_interface_params.CMD_DATA_BLOCK | eth_interface_params.CMD_WR_REQ | timestamp) : eth_interface_params.CMD_WR_REQ;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.CMD_DATA_BLOCK | eth_interface_params.CMD_WR_ACK) : eth_interface_params.CMD_WR_ACK;
        timestamp = 0;
        
        if(broadcast) {
            req_flags |= eth_interface_params.CMD_BROADCAST;
        }

        uint32_t host_dram_block_addr = host_address_params.ETH_ROUTING_BUFFERS_START + (active_core_for_txn * eth_interface_params.CMD_BUF_SIZE + req_wr_ptr) * max_block_size;
        uint16_t host_dram_channel = 0; // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (req_flags & eth_interface_params.CMD_DATA_BLOCK) {
            // Copy data to sysmem or device DRAM for Block mode
            if (use_dram) {
                req_flags |= eth_interface_params.CMD_DATA_BLOCK_DRAM;
                resp_flags |= eth_interface_params.CMD_DATA_BLOCK_DRAM;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
                if(broadcast) {
                    // Write broadcast header to sysmem
                    write_to_sysmem(broadcast_header.data(), broadcast_header.size() * sizeof(uint32_t), host_dram_block_addr, host_dram_channel, mmio_capable_chip_logical);
                }
                // Write payload to sysmem
                write_to_sysmem(data_block, host_dram_block_addr + BROADCAST_HEADER_SIZE * broadcast, host_dram_channel, mmio_capable_chip_logical);

            } else {
                uint32_t buf_address = eth_interface_params.ETH_ROUTING_DATA_BUFFER_ADDR + req_wr_ptr * max_block_size;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
                write_device_memory(data_block.data(), data_block.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, buf_address, write_tlb);
            }
            tt_driver_atomics::sfence();
        }

        // Send the read request
        log_assert(broadcast || (req_flags == eth_interface_params.CMD_WR_REQ) || (((address + offset) % 32) == 0), "Block mode address must be 32-byte aligned."); // Block mode address must be 32-byte aligned.
        
        if(broadcast) {
            // Only specify endpoint local address for broadcast
            new_cmd->sys_addr = address + offset;
        }
        else {
            new_cmd->sys_addr = get_sys_addr(std::get<0>(target_chip), std::get<1>(target_chip), core.x, core.y, address + offset);
            new_cmd->rack = get_sys_rack(std::get<2>(target_chip), std::get<3>(target_chip));
        }
            
        if(req_flags & eth_interface_params.CMD_DATA_BLOCK) {
            // Block mode
            new_cmd->data = block_size + BROADCAST_HEADER_SIZE * broadcast;
        }
        else {
            if(size_in_bytes - offset < sizeof(uint32_t)) {
                // Handle misalignment at the end of the buffer:
                // Assemble a padded uint32_t from single bytes, in case we have less than 4 bytes remaining
                memcpy(&new_cmd->data, static_cast<const uint8_t*>(mem_ptr) + offset, size_in_bytes - offset);
            }
            else {
                new_cmd->data = *((uint32_t*)mem_ptr + offset/DATA_WORD_SIZE);
            }
        }

        new_cmd->flags = req_flags;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        write_device_memory(erisc_command.data(), erisc_command.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_ROUTING_CMD_QUEUE_BASE + (sizeof(routing_cmd_t) * req_wr_ptr), write_tlb);
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.CMD_BUF_PTR_MASK;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, write_tlb);
        tt_driver_atomics::sfence();

        offset += transfer_size;

        // If there is more data to send and this command will make the q full, switch to next Q.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full((erisc_q_ptrs[0]) & eth_interface_params.CMD_BUF_PTR_MASK, erisc_q_rptr[0])) {
            active_core_for_txn++;
            uint32_t update_mask_for_chip = remote_transfer_ethernet_cores[mmio_capable_chip_logical].size() - 1;
            active_core_for_txn = non_mmio_transfer_cores_customized ? (active_core_for_txn & update_mask_for_chip) : ((active_core_for_txn & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID);
            // active_core = (active_core & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID;
            remote_transfer_ethernet_core = remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn];
            read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }
    }
}


// Specialized function for small epoch commands:
// 1) uses separate eth cores than other non-mmio transfers hence does not require mutex
// 2) does not have the code paths for transfers larger than 32kB (1024 cmds)
// 3) only reads erisc_q_ptrs_epoch once, or when the queues are full
// 4) only updates wptr on eth command queues for the last epoch command or when the queue is full or when switching eth cores based on eth-ordered-writes policy, or when
//    eth-ordered-writes are not supported but current write must be ordered (flush prev wrptr).
// 5) When eth-ordered-write not supported, allow flush to be used as ordering mechanism when ordering is requested via arg. When eth-ordered-write is supported, always use it
//    and ensure ordering to same remote chip destinations by always using same remote xfer eth core for a given destination based on noc xy. Must ensure wrptr is flushed on
//    switch of eth cores, and copy of rdptr/wrptr maintained on host for each eth xfer core.
void tt_SiliconDevice::write_to_non_mmio_device_send_epoch_cmd(const uint32_t *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t address, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
    log_assert(!non_mmio_transfer_cores_customized, "{} cannot be used if ethernet cores for host->cluster transfers are customized. The default Ethernet Core configuration must be used.", __FUNCTION__);
    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);

    const auto &mmio_capable_chip = ndesc->get_closest_mmio_capable_chip(core.chip);
    const auto target_chip = ndesc->get_chip_locations().at(core.chip);

    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";
    translate_to_noc_table_coords(0, core.y, core.x);

    const auto &mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(core.chip);
    tt_cxy_pair remote_transfer_ethernet_core = remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_epoch];

    // read all eth queue ptrs for the first time, and initialize wrptr_updated bool for strict ordering.
    if (!erisc_q_ptrs_initialized) {
        for (int core_epoch = EPOCH_ETH_CORES_START_ID; core_epoch < EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS + EPOCH_ETH_CORES_START_ID; core_epoch++) {
            erisc_q_ptrs_epoch[core_epoch].reserve(eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2/sizeof(uint32_t));
            read_device_memory(erisc_q_ptrs_epoch[core_epoch].data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
            erisc_q_wrptr_updated[core_epoch] = false;
            erisc_q_ptrs_initialized = true;
        }
    }

    std::vector<std::uint32_t> erisc_command(sizeof(routing_cmd_t)/DATA_WORD_SIZE);
    routing_cmd_t *new_cmd = (routing_cmd_t *)&erisc_command[0];
    std::vector<std::uint32_t> data_block;

    // Two mechanisms for ordering depending on eth fw version.
    if (use_ethernet_ordered_writes) {
        // Feature in this function to ensure ordering via eth-ordered-writes by using same eth core for all epoch writes to same dest noc xy.
        auto &soc_desc  = get_soc_descriptor(mmio_capable_chip);
        int core_id = core.x * soc_desc.grid_size.y + core.y;
        int new_active_core_epoch = (core_id % EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS) + EPOCH_ETH_CORES_START_ID;

        // Switch eth cores, and if wrptr was not flushed to device for previous eth core, do it now.
        if (new_active_core_epoch != active_core_epoch) {
            if (!erisc_q_wrptr_updated[active_core_epoch]) {
                std::vector<std::uint32_t> erisc_q_wptr = { erisc_q_ptrs_epoch[active_core_epoch][0] };
                write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, write_tlb);
                tt_driver_atomics::sfence();
                erisc_q_wrptr_updated[active_core_epoch] = true;
            }
            active_core_epoch = new_active_core_epoch;
            remote_transfer_ethernet_core = remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_epoch];
        }
    } else if (ordered_with_prev_remote_write) {
        // Flush used as ordering mechanism when eth ordered writes are unsupported. If previous write requires flush,
        // handle it here before setting flush_non_mmio for the current write.
        if (!erisc_q_wrptr_updated[active_core_epoch]) {
            std::vector<std::uint32_t> erisc_q_wptr = { erisc_q_ptrs_epoch[active_core_epoch][0] };
            write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, write_tlb);
            tt_driver_atomics::sfence();
            erisc_q_wrptr_updated[active_core_epoch] = true;
        }
        wait_for_non_mmio_flush();
    }

    flush_non_mmio = true;
    uint32_t timestamp = 0; //CMD_TIMESTAMP;

    bool use_dram = size_in_bytes > 256 * DATA_WORD_SIZE ? true : false;
    uint32_t max_block_size = use_dram ? host_address_params.ETH_ROUTING_BLOCK_SIZE : eth_interface_params.MAX_BLOCK_SIZE;
    uint32_t block_size;

    // Ethernet ordered writes must originate from same erisc core, so prevent updating active core here.
    while (is_non_mmio_cmd_q_full(erisc_q_ptrs_epoch[active_core_epoch][0], erisc_q_ptrs_epoch[active_core_epoch][4])) {
        if (!use_ethernet_ordered_writes){
            active_core_epoch++;
            log_assert(active_core_epoch - EPOCH_ETH_CORES_START_ID >= 0, "Invalid ERISC core for sending epoch commands");
            active_core_epoch = ((active_core_epoch - EPOCH_ETH_CORES_START_ID) % EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS) + EPOCH_ETH_CORES_START_ID;
            remote_transfer_ethernet_core = remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_epoch];
        }
        read_device_memory(erisc_q_ptrs_epoch[active_core_epoch].data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
    }

    uint32_t req_wr_ptr = erisc_q_ptrs_epoch[active_core_epoch][0] & eth_interface_params.CMD_BUF_SIZE_MASK;
    if (address & 0x1F) { // address not 32-byte aligned
        // can send it in one transfer, no need to break it up
        log_assert(size_in_bytes == DATA_WORD_SIZE, "Non-mmio cmd queue update is too big");
        block_size = DATA_WORD_SIZE;
    } else {
        // can send it in one transfer, no need to break it up
        log_assert(size_in_bytes <= max_block_size, "Non-mmio cmd queue update is too big. size_in_bytes: {} exceeds max_block_size: {}", size_in_bytes, max_block_size);
        block_size = size_in_bytes;
    }
    uint32_t req_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.CMD_DATA_BLOCK | eth_interface_params.CMD_WR_REQ | timestamp) : eth_interface_params.CMD_WR_REQ;
    if (use_ethernet_ordered_writes) {
        req_flags |= eth_interface_params.CMD_ORDERED;
    }

    uint32_t resp_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.CMD_DATA_BLOCK | eth_interface_params.CMD_WR_ACK) : eth_interface_params.CMD_WR_ACK;
    timestamp = 0;

    uint32_t host_dram_block_addr = host_address_params.ETH_ROUTING_BUFFERS_START + (active_core_epoch * eth_interface_params.CMD_BUF_SIZE + req_wr_ptr) * max_block_size;
    uint16_t host_dram_channel = 0; // This needs to be 0, since WH can only map ETH buffers to chan 0.

    // send the data
    if (req_flags & eth_interface_params.CMD_DATA_BLOCK) {
        // Copy data to sysmem or device DRAM for Block mode
        if (use_dram) {
            req_flags |= eth_interface_params.CMD_DATA_BLOCK_DRAM;
            resp_flags |= eth_interface_params.CMD_DATA_BLOCK_DRAM;
            size_buffer_to_capacity(data_block, block_size);
            memcpy(&data_block[0], mem_ptr, block_size);
            write_to_sysmem(data_block, host_dram_block_addr, host_dram_channel, mmio_capable_chip_logical);
        } else {
            uint32_t buf_address = eth_interface_params.ETH_ROUTING_DATA_BUFFER_ADDR + req_wr_ptr * max_block_size;
            size_buffer_to_capacity(data_block, block_size);
            memcpy(&data_block[0], mem_ptr, block_size);
            write_device_memory(data_block.data(), data_block.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, buf_address, write_tlb);
        }
        tt_driver_atomics::sfence();
    }

    // send the write request
    log_assert((req_flags & eth_interface_params.CMD_DATA_BLOCK) ? (address & 0x1F) == 0 : true, "Block mode address must be 32-byte aligned.");

    new_cmd->sys_addr = get_sys_addr(std::get<0>(target_chip), std::get<1>(target_chip), core.x, core.y, address);
    new_cmd->rack = get_sys_rack(std::get<2>(target_chip), std::get<3>(target_chip));
    new_cmd->data = req_flags & eth_interface_params.CMD_DATA_BLOCK ? block_size : *mem_ptr;
    new_cmd->flags = req_flags;
    if (use_dram) {
        new_cmd->src_addr_tag = host_dram_block_addr;
    }

    write_device_memory(erisc_command.data(), erisc_command.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_ROUTING_CMD_QUEUE_BASE + (sizeof(routing_cmd_t) * req_wr_ptr), write_tlb);
    tt_driver_atomics::sfence();

    // update the wptr only if the eth queue is full or for the last command
    erisc_q_ptrs_epoch[active_core_epoch][0] = (erisc_q_ptrs_epoch[active_core_epoch][0] + 1) & eth_interface_params.CMD_BUF_PTR_MASK;
    if (last_send_epoch_cmd || is_non_mmio_cmd_q_full(erisc_q_ptrs_epoch[active_core_epoch][0], erisc_q_ptrs_epoch[active_core_epoch][4])) {
        std::vector<std::uint32_t> erisc_q_wptr = { erisc_q_ptrs_epoch[active_core_epoch][0] };
        write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, write_tlb);
        tt_driver_atomics::sfence();
        erisc_q_wrptr_updated[active_core_epoch] = true;
    } else {
        erisc_q_wrptr_updated[active_core_epoch] = false;
    }
}

/*
 * Note that this function is required to acquire the `NON_MMIO_MUTEX_NAME` mutex for interacting with the ethernet core (host) command queue
 * DO NOT issue any pcie reads/writes to the ethernet core prior to acquiring the mutex. For extra information, see the "NON_MMIO_MUTEX Usage" above
 */
void tt_SiliconDevice::rolled_write_to_non_mmio_device(const uint32_t *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t address, uint32_t unroll_count) {
    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);

    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";
    translate_to_noc_table_coords(0, core.y, core.x);

    const eth_coord_t target_chip = ndesc->get_chip_locations().at(core.chip);


    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_q_ptrs = std::vector<uint32_t>(eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2 / sizeof(uint32_t));

    std::vector<std::uint32_t> data_block = std::vector<uint32_t>(size_in_bytes / DATA_WORD_SIZE);

    routing_cmd_t *new_cmd;

    flush_non_mmio = true;
    uint32_t transfer_size = size_in_bytes * unroll_count;
    uint32_t buffer_id = 0;
    uint32_t timestamp = 0; //CMD_TIMESTAMP;

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    const auto &mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(core.chip);

    if (non_mmio_transfer_cores_customized) {
        log_assert(active_eth_core_idx_per_chip.find(mmio_capable_chip_logical) != active_eth_core_idx_per_chip.end(), "Ethernet Cores for Host to Cluster communication were not initialized for all MMIO devices.");
    }

    const scoped_lock<named_mutex> lock(
        *get_mutex(NON_MMIO_MUTEX_NAME, this->get_pci_device(mmio_capable_chip_logical)->id));

    erisc_command.resize(sizeof(routing_cmd_t)/DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t *)&erisc_command[0];
    int& active_core_for_txn = non_mmio_transfer_cores_customized ? active_eth_core_idx_per_chip.at(mmio_capable_chip_logical) : active_core;
    read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn], eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);

    uint32_t offset = 0;

    bool full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];

    uint32_t unroll_offset = 0;

    while (offset < transfer_size) {
        while (full) {
            read_device_memory(erisc_q_rptr.data(), remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn], eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES + eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES, DATA_WORD_SIZE, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0],erisc_q_rptr[0]);
        }
        //full = true;
        // set full only if this command will make the q full.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.
        //full = is_non_mmio_cmd_q_full((erisc_q_ptrs[0] + 1) & CMD_BUF_PTR_MASK, erisc_q_rptr[0]);

        log_assert(((address + offset) & 0x1F) == 0, "Base address + offset in incorrect range!");

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.CMD_BUF_SIZE_MASK;

        uint32_t req_flags = eth_interface_params.CMD_DATA_BLOCK_DRAM | eth_interface_params.CMD_DATA_BLOCK | eth_interface_params.CMD_WR_REQ;
        timestamp = 0;

        uint32_t host_dram_block_addr = host_address_params.ETH_ROUTING_BUFFERS_START + (active_core_for_txn * eth_interface_params.CMD_BUF_SIZE + req_wr_ptr) * host_address_params.ETH_ROUTING_BLOCK_SIZE;
        uint16_t host_dram_channel = 0; // This needs to be 0, since WH can only map ETH buffers to chan 0.

        memcpy(data_block.data(), mem_ptr, size_in_bytes);
        uint32_t byte_increment = data_block.size() * DATA_WORD_SIZE;
        uint32_t host_mem_offset = 0;
        uint32_t i = 0;
        for (i = 0; (i + unroll_offset) < unroll_count; i++) {
            if ((host_mem_offset + byte_increment) > host_address_params.ETH_ROUTING_BLOCK_SIZE) {
                break;
            }
            data_block[0] = i + unroll_offset;
            write_to_sysmem(data_block, host_dram_block_addr + host_mem_offset, host_dram_channel, mmio_capable_chip_logical);
            host_mem_offset += byte_increment;
        }
        unroll_offset += i;
        tt_driver_atomics::sfence();
        new_cmd->sys_addr = get_sys_addr(std::get<0>(target_chip), std::get<1>(target_chip), core.x, core.y, address + offset);
        new_cmd->rack = get_sys_rack(std::get<2>(target_chip), std::get<3>(target_chip));
        new_cmd->data = host_mem_offset;
        new_cmd->flags = req_flags;
        new_cmd->src_addr_tag = host_dram_block_addr;

        write_device_memory(erisc_command.data(), erisc_command.size() * DATA_WORD_SIZE, remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn], eth_interface_params.REQUEST_ROUTING_CMD_QUEUE_BASE + (sizeof(routing_cmd_t) * req_wr_ptr), write_tlb);
        tt_driver_atomics::sfence();
        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.CMD_BUF_PTR_MASK;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn], eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, write_tlb);
        tt_driver_atomics::sfence();
        offset += host_mem_offset;

        // If there is more data to send and this command will make the q full, switch to next Q.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full((erisc_q_ptrs[0]) & eth_interface_params.CMD_BUF_PTR_MASK, erisc_q_rptr[0])) {
            active_core_for_txn++;
            uint32_t update_mask_for_chip = (remote_transfer_ethernet_cores[mmio_capable_chip_logical].size() - 1);
            active_core_for_txn = non_mmio_transfer_cores_customized ? (active_core_for_txn & update_mask_for_chip) : ((active_core_for_txn & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID);
            read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn], eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }
    }
}

/*
 * Note that this function is required to acquire the `NON_MMIO_MUTEX_NAME` mutex for interacting with the ethernet core (host) command queue
 * DO NOT use `active_core` or issue any pcie reads/writes to the ethernet core prior to acquiring the mutex. For extra information, see the "NON_MMIO_MUTEX Usage" above
 */
void tt_SiliconDevice::read_from_non_mmio_device(void* mem_ptr, tt_cxy_pair core, uint64_t address, uint32_t size_in_bytes) {

    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);
    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";
    translate_to_noc_table_coords(*this->get_target_mmio_device_ids().begin(), core.y, core.x);

    const auto &mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(core.chip);
    const eth_coord_t target_chip = ndesc->get_chip_locations().at(core.chip);

    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr;
    std::vector<std::uint32_t> erisc_q_ptrs = std::vector<uint32_t>(eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2 / DATA_WORD_SIZE);
    std::vector<std::uint32_t> erisc_resp_q_wptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_resp_q_rptr = std::vector<uint32_t>(1);


    std::vector<std::uint32_t> data_block;

    routing_cmd_t *new_cmd;

    erisc_command.resize(sizeof(routing_cmd_t)/DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t *)&erisc_command[0];

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    const scoped_lock<named_mutex> lock(
        *get_mutex(NON_MMIO_MUTEX_NAME, this->get_pci_device(mmio_capable_chip_logical)->id));
    const tt_cxy_pair remote_transfer_ethernet_core = remote_transfer_ethernet_cores[mmio_capable_chip_logical].at(0);

    read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
    read_device_memory(erisc_resp_q_wptr.data(), remote_transfer_ethernet_core, eth_interface_params.RESPONSE_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, DATA_WORD_SIZE, read_tlb);
    read_device_memory(erisc_resp_q_rptr.data(), remote_transfer_ethernet_core, eth_interface_params.RESPONSE_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES + eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES, DATA_WORD_SIZE, read_tlb);

    bool full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];

    bool use_dram;
    uint32_t max_block_size;

    use_dram = size_in_bytes > 1024;
    max_block_size = use_dram ? host_address_params.ETH_ROUTING_BLOCK_SIZE : eth_interface_params.MAX_BLOCK_SIZE;

    uint32_t offset = 0;
    uint32_t block_size;
    uint32_t buffer_id = 0;

    while (offset < size_in_bytes) {
        while (full) {
            read_device_memory(erisc_q_rptr.data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES + eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES, DATA_WORD_SIZE, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0],erisc_q_rptr[0]);
        }

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.CMD_BUF_SIZE_MASK;
        if ((address + offset) & 0x1F) { // address not 32-byte aligned
            block_size = DATA_WORD_SIZE; // 4 byte aligned block
        } else {
            block_size = offset + max_block_size > size_in_bytes ? size_in_bytes - offset : max_block_size;
            // Align up to 4 bytes.
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;

        }
        uint32_t req_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.CMD_DATA_BLOCK | eth_interface_params.CMD_RD_REQ) : eth_interface_params.CMD_RD_REQ;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.CMD_DATA_BLOCK | eth_interface_params.CMD_RD_DATA) : eth_interface_params.CMD_RD_DATA;
        uint32_t resp_rd_ptr = erisc_resp_q_rptr[0] & eth_interface_params.CMD_BUF_SIZE_MASK;
        uint32_t host_dram_block_addr = host_address_params.ETH_ROUTING_BUFFERS_START + resp_rd_ptr * max_block_size;
        uint16_t host_dram_channel = 0; // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (use_dram && block_size > DATA_WORD_SIZE) {
            req_flags |= eth_interface_params.CMD_DATA_BLOCK_DRAM;
            resp_flags |= eth_interface_params.CMD_DATA_BLOCK_DRAM;
        }

        // Send the read request
        log_assert((req_flags == eth_interface_params.CMD_RD_REQ) || (((address + offset) & 0x1F) == 0), "Block mode offset must be 32-byte aligned."); // Block mode offset must be 32-byte aligned.
        new_cmd->sys_addr = get_sys_addr(std::get<0>(target_chip), std::get<1>(target_chip), core.x, core.y, address + offset);
        new_cmd->rack = get_sys_rack(std::get<2>(target_chip), std::get<3>(target_chip));
        new_cmd->data = block_size;
        new_cmd->flags = req_flags;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        write_device_memory(erisc_command.data(), erisc_command.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_ROUTING_CMD_QUEUE_BASE + (sizeof(routing_cmd_t) * req_wr_ptr), write_tlb);;
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.CMD_BUF_PTR_MASK;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, write_tlb);
        tt_driver_atomics::sfence();
        // If there is more data to read and this command will make the q full, set full to 1.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full((erisc_q_ptrs[0]), erisc_q_rptr[0])) {
            read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }

        // Wait for read request completion and extract the data into the `mem_ptr`

        // erisc firmware will:
        // 1. clear response flags
        // 2. start operation
        // 3. advance response wrptr
        // 4. complete operation and write data into response or buffer
        // 5. set response flags
        // So we have to wait for wrptr to advance, then wait for flags to be nonzero, then read data.

        do {
            read_device_memory(erisc_resp_q_wptr.data(), remote_transfer_ethernet_core, eth_interface_params.RESPONSE_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, DATA_WORD_SIZE, read_tlb);
        } while (erisc_resp_q_rptr[0] == erisc_resp_q_wptr[0]);
        tt_driver_atomics::lfence();
        uint32_t flags_offset = 12 + sizeof(routing_cmd_t) * resp_rd_ptr;
        std::vector<std::uint32_t> erisc_resp_flags = std::vector<uint32_t>(1);
        do {
            read_device_memory(erisc_resp_flags.data(), remote_transfer_ethernet_core, eth_interface_params.RESPONSE_ROUTING_CMD_QUEUE_BASE + flags_offset, DATA_WORD_SIZE, read_tlb);
        } while (erisc_resp_flags[0] == 0);

        if (erisc_resp_flags[0] == resp_flags) {
            tt_driver_atomics::lfence();
            uint32_t data_offset = 8 + sizeof(routing_cmd_t) * resp_rd_ptr;
            if (block_size == DATA_WORD_SIZE) {
                std::vector<std::uint32_t> erisc_resp_data = std::vector<uint32_t>(1);
                read_device_memory(erisc_resp_data.data(), remote_transfer_ethernet_core, eth_interface_params.RESPONSE_ROUTING_CMD_QUEUE_BASE + data_offset, DATA_WORD_SIZE, read_tlb);
                if(size_in_bytes - offset < 4)  {
                    // Handle misaligned (4 bytes) data at the end of the block.
                    // Only read remaining bytes into the host buffer, instead of reading the full uint32_t
                    std::memcpy((uint8_t*)mem_ptr + offset, erisc_resp_data.data(), size_in_bytes - offset);
                }
                else {
                    *((uint32_t*)mem_ptr + offset/DATA_WORD_SIZE) = erisc_resp_data[0];
                }
            } else {
                // Read 4 byte aligned block from device/sysmem
                if (use_dram) {
                    read_from_sysmem(data_block, host_dram_block_addr, host_dram_channel, block_size, mmio_capable_chip_logical);
                } else {
                    uint32_t buf_address = eth_interface_params.ETH_ROUTING_DATA_BUFFER_ADDR + resp_rd_ptr * max_block_size;
                    size_buffer_to_capacity(data_block, block_size);
                    read_device_memory(data_block.data(), remote_transfer_ethernet_core, buf_address, block_size, read_tlb);
                }
                // assert(mem_ptr.size() - (offset/DATA_WORD_SIZE) >= (block_size * DATA_WORD_SIZE));
                log_assert((data_block.size() * DATA_WORD_SIZE) >= block_size, "Incorrect data size read back from sysmem/device");
                // Account for misalignment by skipping any padding bytes in the copied data_block
                memcpy((uint8_t*)mem_ptr + offset, data_block.data(), std::min(block_size, size_in_bytes - offset));
            }
        }

        // Finally increment the rdptr for the response command q
        erisc_resp_q_rptr[0] = (erisc_resp_q_rptr[0] + 1) & eth_interface_params.CMD_BUF_PTR_MASK;
        write_device_memory(erisc_resp_q_rptr.data(), erisc_resp_q_rptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.RESPONSE_CMD_QUEUE_BASE + sizeof(remote_update_ptr_t) + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, write_tlb);
        tt_driver_atomics::sfence();
        log_assert(erisc_resp_flags[0] == resp_flags, "Unexpected ERISC Response Flags.");

        offset += block_size;
    }

}

void tt_SiliconDevice::wait_for_non_mmio_flush() {
    if(flush_non_mmio) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");
        std::string read_tlb = "LARGE_READ_TLB";
        auto chips_with_mmio = this->get_target_mmio_device_ids();
        for(auto chip_id : chips_with_mmio) {
            auto arch = get_soc_descriptor(chip_id).arch;
            if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
                std::vector<std::uint32_t> erisc_txn_counters = std::vector<uint32_t>(2);
                std::vector<std::uint32_t> erisc_q_ptrs = std::vector<uint32_t>(eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2 / sizeof(uint32_t));

                //wait for all queues to be empty.
                for (int i = 0; i < NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS; i++) {
                    do {
                        read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_cores.at(chip_id)[i], eth_interface_params.REQUEST_CMD_QUEUE_BASE + eth_interface_params.CMD_COUNTERS_SIZE_BYTES, eth_interface_params.REMOTE_UPDATE_PTR_SIZE_BYTES*2, read_tlb);
                    } while (erisc_q_ptrs[0] != erisc_q_ptrs[4]);
                }
                //wait for all write responses to come back.
                for (int i = 0; i < NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS; i++) {
                    do {
                        read_device_memory(erisc_txn_counters.data(), remote_transfer_ethernet_cores.at(chip_id)[i], eth_interface_params.REQUEST_CMD_QUEUE_BASE, 8, read_tlb);
                    } while (erisc_txn_counters[0] != erisc_txn_counters[1]);
                }
            } else {
                break;
            }
        }
        flush_non_mmio = false;
    }
}

// Broadcast Functions
void tt_SiliconDevice::generate_tensix_broadcast_grids_for_grayskull(std::set<std::pair<tt_xy_pair, tt_xy_pair>>& broadcast_grids,  std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude) {
    // If row 0 is not explicitly excluded, exclude it here since its non-tensix
    rows_to_exclude.insert(0);
    // If row 11 is excluded, we can close the SOC grid. If not, exclude row 12 to close grid.
    if(rows_to_exclude.find(11) == rows_to_exclude.end()) {
        rows_to_exclude.insert(12);
    }
    // If col 0 is not explicitly excluded, exclude it here since its non-tensix
    cols_to_exclude.insert(0);
    // If col 12 is excluded, we can close the SOC grid. If not, exclude col 13 to close grid.
    if(cols_to_exclude.find(12) == cols_to_exclude.end()) {
        cols_to_exclude.insert(13);
    }
    std::vector<std::pair<int, int>> bb_x_coords = {};
    std::vector<std::pair<int, int>> bb_y_coords = {};

    // Generate starting and ending x coordinates of each bounding box/grid
    for(auto x_it = cols_to_exclude.begin(); x_it != cols_to_exclude.end(); x_it++) {
        if(x_it == std::prev(cols_to_exclude.end(), 1)) continue;
        if(cols_to_exclude.find(*(x_it) + 1) == cols_to_exclude.end() and cols_to_exclude.find(*(std::next(x_it, 1)) - 1) == cols_to_exclude.end()) {
            bb_x_coords.push_back({*(x_it) + 1, *(std::next(x_it, 1)) - 1});
        }
    }

    for(auto y_it = rows_to_exclude.begin(); y_it != rows_to_exclude.end(); y_it++) {
        if(y_it == std::prev(rows_to_exclude.end(), 1)) continue;
        if(rows_to_exclude.find((*y_it) + 1) == rows_to_exclude.end() and rows_to_exclude.find(*std::next(y_it, 1) - 1) == rows_to_exclude.end()) {
            bb_y_coords.push_back({*(y_it) + 1, *(std::next(y_it, 1)) - 1});
        }
    }
    // Assemble x and y coordinates into bounding box vertices
    for(const auto& x_pair : bb_x_coords) {
        for(const auto& y_pair : bb_y_coords) {
            tt_xy_pair top_left = tt_xy_pair(x_pair.first, y_pair.first);
            tt_xy_pair bot_right = tt_xy_pair(x_pair.second, y_pair.second);
            broadcast_grids.insert({top_left, bot_right});
        }
    }
}

std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& tt_SiliconDevice::get_ethernet_broadcast_headers(const std::set<chip_id_t>& chips_to_exclude) {
    // Generate headers for Ethernet Broadcast (WH) only. Each header corresponds to a unique broadcast "grid".
    if(bcast_header_cache.find(chips_to_exclude) == bcast_header_cache.end()) {
        bcast_header_cache[chips_to_exclude] = {};
        std::unordered_map<chip_id_t, std::unordered_map<chip_id_t, std::vector<int>>> broadcast_mask_for_target_chips_per_group = {};
        std::map<std::vector<int>, std::tuple<chip_id_t, std::vector<int>>> broadcast_header_union_per_group = {};
        chip_id_t first_mmio_chip = *(get_target_mmio_device_ids().begin());
        for(const auto& chip : target_devices_in_cluster) {
            if(chips_to_exclude.find(chip) == chips_to_exclude.end()) {
                // Get shelf local physical chip id included in broadcast
                chip_id_t physical_chip_id = ndesc -> get_shelf_local_physical_chip_coords(chip);
                eth_coord_t eth_coords = ndesc -> get_chip_locations().at(chip);
                // Rack word to be set in header
                uint32_t rack_word = std::get<2>(eth_coords) >> 2;
                // Rack byte to be set in header
                uint32_t rack_byte = std::get<2>(eth_coords) % 4;
                // 1st level grouping: Group broadcasts based on the MMIO chip they must go through
                // Nebula + Galaxy Topology assumption: Disjoint sets can only be present in the first shelf, with each set connected to host through its closest MMIO chip
                // For the first shelf, pass broadcasts to specific chips through their closest MMIO chip
                // All other shelves are fully connected galaxy grids. These are connected to all MMIO devices. Use any (or the first) MMIO device in the list.
                chip_id_t closest_mmio_chip = 0;
                if (std::get<2>(eth_coords) == 0 && std::get<3>(eth_coords) == 0) {
                    // Shelf 0 + Rack 0: Either an MMIO chip or a remote chip potentially connected to host through its own MMIO counterpart.
                    closest_mmio_chip = ndesc -> get_closest_mmio_capable_chip(chip);
                }
                else {
                    // All other shelves: Group these under the same/first MMIO chip, since all MMIO chips are connected.
                    closest_mmio_chip = first_mmio_chip;
                }
                if(broadcast_mask_for_target_chips_per_group.find(closest_mmio_chip) == broadcast_mask_for_target_chips_per_group.end()) {
                    broadcast_mask_for_target_chips_per_group.insert({closest_mmio_chip, {}});
                }
                // For each target physical chip id (local to a shelf), generate headers based on all racks and shelves that contain this physical id.
                if(broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).find(physical_chip_id) == broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).end()) {
                    // Target seen for the first time.
                    std::vector<int> broadcast_mask(8, 0);
                    broadcast_mask.at(rack_word) |= (1 << std::get<3>(eth_coords)) << rack_byte;
                    broadcast_mask.at(3) |= 1 << physical_chip_id;
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).insert({physical_chip_id, broadcast_mask});

                }
                else {
                    // Target was seen before -> include curr rack and shelf in header
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).at(physical_chip_id).at(rack_word) |= static_cast<uint32_t>(1 << std::get<3>(eth_coords)) << rack_byte;
                }
            }
        }
        // 2nd level grouping: For each MMIO group, further group the chips based on their rack and shelf headers. The number of groups after this step represent the final set of broadcast grids.
        for(auto& mmio_group : broadcast_mask_for_target_chips_per_group) {
            for(auto& chip : mmio_group.second) {
                // Generate a hash for this MMIO Chip + Rack + Shelf group
                std::vector<int> header_hash = {mmio_group.first, chip.second.at(0), chip.second.at(1), chip.second.at(2)};
                if(broadcast_header_union_per_group.find(header_hash) == broadcast_header_union_per_group.end()) {
                    broadcast_header_union_per_group.insert({header_hash, std::make_tuple(mmio_group.first, chip.second)});
                }
                else {
                    // If group found, update chip header entry
                    std::get<1>(broadcast_header_union_per_group.at(header_hash)).at(3) |= chip.second.at(3);
                }
            }
        }
        // Get all broadcast headers per MMIO group
        for(const auto& header : broadcast_header_union_per_group) {
            chip_id_t mmio_chip = std::get<0>(header.second);
            if(bcast_header_cache[chips_to_exclude].find(mmio_chip) == bcast_header_cache[chips_to_exclude].end()) {
                bcast_header_cache[chips_to_exclude].insert({mmio_chip, {}});
            }
            bcast_header_cache[chips_to_exclude].at(mmio_chip).push_back(std::get<1>(header.second));
        }
        // Invert headers (FW convention)
        for(auto& bcast_group : bcast_header_cache[chips_to_exclude]) {
            for(auto& header : bcast_group.second) {
                int header_idx = 0;
                for(auto& header_entry : header) {
                    if(header_idx == 4) break;
                    header_entry = ~header_entry;
                    header_idx++;
                }
            }
        }
    }
    return bcast_header_cache[chips_to_exclude];
}

void tt_SiliconDevice::pcie_broadcast_write(chip_id_t chip, const void* mem_ptr, uint32_t size_in_bytes, std::uint32_t addr, const tt_xy_pair& start, const tt_xy_pair& end, const std::string& fallback_tlb) {
    // Use the specified TLB to broadcast data to all cores included in the [start, end] grid -> GS Only. Use Ethernet Broadcast for WH.
    struct PCIdevice* pci_device = get_pci_device(chip);
    const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
    TTDevice *dev = pci_device->hdev;
    const uint8_t* buffer_addr = static_cast<const uint8_t*>(mem_ptr);
    const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device -> id));
    while(size_in_bytes > 0) {
        auto [mapped_address, tlb_size] = set_dynamic_tlb_broadcast(pci_device, tlb_index, addr, harvested_coord_translation, start, end, dynamic_tlb_ordering_modes.at(fallback_tlb));
        uint32_t transfer_size = std::min(size_in_bytes, tlb_size);
        write_block(dev, mapped_address, transfer_size, buffer_addr, m_dma_buf_size);

        size_in_bytes -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

inline bool tensix_or_eth_in_broadcast(const std::set<uint32_t>& cols_to_exclude, const tt::umd::architecture_implementation* architecture_implementation) {
    bool found_tensix_or_eth = false;
    for(const auto& col : architecture_implementation->get_t6_x_locations()) {
        found_tensix_or_eth |= (cols_to_exclude.find(col) == cols_to_exclude.end());
    }
    return found_tensix_or_eth;
}

inline bool valid_tensix_broadcast_grid(const std::set<uint32_t>& rows_to_exclude, const std::set<uint32_t>& cols_to_exclude, const tt::umd::architecture_implementation* architecture_implementation) {
    bool t6_bcast_rows_complete = true;
    bool t6_bcast_rows_empty = true;
    
    for(const auto& row : architecture_implementation->get_t6_y_locations()) {
        t6_bcast_rows_complete &= (rows_to_exclude.find(row) == rows_to_exclude.end());
        t6_bcast_rows_empty &= (rows_to_exclude.find(row) != rows_to_exclude.end());
    }
    return t6_bcast_rows_complete || t6_bcast_rows_empty;
}


void tt_SiliconDevice::ethernet_broadcast_write(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address,
                                                const std::set<chip_id_t>& chips_to_exclude, const std::set<uint32_t>& rows_to_exclude, 
                                                std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb, bool use_virtual_coords) {
    if(use_ethernet_broadcast) {
        // Broadcast through ERISC core supported
        std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& broadcast_headers = get_ethernet_broadcast_headers(chips_to_exclude);
        // Apply row and column exclusion mask explictly. Placing this here if we want to cache the higher level broadcast headers on future/
        std::uint32_t row_exclusion_mask = 0;
        std::uint32_t col_exclusion_mask = 0;
        for(const auto& row : rows_to_exclude) {
            row_exclusion_mask |= 1 << row;
        }

        for(const auto& col : cols_to_exclude) {
            col_exclusion_mask |= 1 << (16 + col);
        }
        // Write broadcast block to device.
        for(auto& mmio_group : broadcast_headers) {
            for(auto& header : mmio_group.second) {
                header.at(4) = use_virtual_coords * 0x8000; // Reset row/col exclusion masks
                header.at(4) |= row_exclusion_mask;
                header.at(4) |= col_exclusion_mask;
                // Write Target: x-y endpoint is a don't care. Initialize to tt_xy_pair(1, 1)
                write_to_non_mmio_device(mem_ptr, size_in_bytes, tt_cxy_pair(mmio_group.first, tt_xy_pair(1, 1)), address, true, header);
            }
        }
    }
    else {
        // Broadcast not supported. Implement this at the software level as a for loop
        std::vector<tt_cxy_pair> cores_to_write = {};
        for(const auto& chip : target_devices_in_cluster) {
            if(chips_to_exclude.find(chip) != chips_to_exclude.end()) continue;
            for(const auto& core : get_soc_descriptor(chip).cores) {
                if(cols_to_exclude.find(core.first.x) == cols_to_exclude.end() and rows_to_exclude.find(core.first.y) == rows_to_exclude.end() and core.second.type != CoreType::HARVESTED) {
                    write_to_device(mem_ptr, size_in_bytes, tt_cxy_pair(chip, core.first.x, core.first.y), address, fallback_tlb);
                }
            }
        }
    }
}

void tt_SiliconDevice::broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address,
                       const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb) {
    if (arch_name == tt::ARCH::GRAYSKULL) {
        // Device FW disables broadcasts to all non tensix cores.
        std::vector<tt_xy_pair> dram_cores_to_write = {};
        std::vector<uint32_t> dram_rows = {0, 6};
        std::vector<uint32_t> dram_cols = {1, 4, 7, 10};

        for(const auto& row : dram_rows) {
            for(const auto& col : dram_cols) {
                if(rows_to_exclude.find(row) == rows_to_exclude.end() and cols_to_exclude.find(col) == cols_to_exclude.end()) {
                    dram_cores_to_write.push_back(tt_xy_pair(col, row));
                }
            }
        }
        
        std::set<std::pair<tt_xy_pair, tt_xy_pair>> broadcast_grids = {};
        generate_tensix_broadcast_grids_for_grayskull(broadcast_grids, rows_to_exclude, cols_to_exclude);
        for(const auto& chip : target_devices_in_cluster) {
            if(chips_to_exclude.find(chip) != chips_to_exclude.end()) continue;
            for(const auto& dram : dram_cores_to_write) {
                write_device_memory(mem_ptr, size_in_bytes, tt_cxy_pair(chip, dram), address, fallback_tlb);
            }
            for(const auto& grid : broadcast_grids) {
                pcie_broadcast_write(chip, mem_ptr, size_in_bytes, address, grid.first, grid.second, fallback_tlb);
            }
        } 
    }
    else if (arch_name == tt::ARCH::BLACKHOLE) {
        auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
        if(cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(9) == cols_to_exclude.end()) {
            log_assert(!tensix_or_eth_in_broadcast(cols_to_exclude, architecture_implementation.get()), "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Wormhole.");
            if(cols_to_exclude.find(0) == cols_to_exclude.end()) {
                // When broadcast includes column zero do not exclude anything
                std::set<uint32_t> unsafe_rows = {};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = cols_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(9);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude_for_col_0_bcast, cols_to_exclude_for_col_0_bcast, fallback_tlb, false);
            }
            if(cols_to_exclude.find(9) == cols_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_9_bcast = cols_to_exclude;
                cols_to_exclude_for_col_9_bcast.insert(0);
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude, cols_to_exclude_for_col_9_bcast, fallback_tlb, false);
            }
        }
        else {
            log_assert(use_virtual_coords_for_eth_broadcast or valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()), 
                        "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                    rows_to_exclude, cols_to_exclude, fallback_tlb, use_virtual_coords_for_eth_broadcast);
        }
    }
    else {
        auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
        if(cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(5) == cols_to_exclude.end()) {
            log_assert(!tensix_or_eth_in_broadcast(cols_to_exclude, architecture_implementation.get()), "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Wormhole.");
            if(cols_to_exclude.find(0) == cols_to_exclude.end()) {
                // When broadcast includes column zero Exclude PCIe, ARC and router cores from broadcast explictly, since writing to these is unsafe
                // ERISC FW does not exclude these.
                std::set<uint32_t> unsafe_rows = {2, 3, 4, 8, 9, 10};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = cols_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(5);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude_for_col_0_bcast, cols_to_exclude_for_col_0_bcast, fallback_tlb, false);
            }
            if(cols_to_exclude.find(5) == cols_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_5_bcast = cols_to_exclude;
                cols_to_exclude_for_col_5_bcast.insert(0);
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude, cols_to_exclude_for_col_5_bcast, fallback_tlb, false);
            }
        }
        else {
            log_assert(use_virtual_coords_for_eth_broadcast or valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()), 
                        "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                    rows_to_exclude, cols_to_exclude, fallback_tlb, use_virtual_coords_for_eth_broadcast);
        }
    }
}

int tt_SiliconDevice::remote_arc_msg(int chip, uint32_t msg_code, bool wait_for_done, uint32_t arg0, uint32_t arg1, int timeout, uint32_t *return_3, uint32_t *return_4) {
    constexpr uint64_t ARC_RESET_SCRATCH_ADDR = 0x880030060;
    constexpr uint64_t ARC_RESET_MISC_CNTL_ADDR = 0x880030100;

    auto core = tt_cxy_pair(chip, get_soc_descriptor(chip).arc_cores.at(0));

    if ((msg_code & 0xff00) != 0xaa00) {
        log_error("Malformed message. msg_code is 0x{:x} but should be 0xaa..\n", msg_code);
    }
    log_assert (arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args"); // Only 16 bits are allowed

    uint32_t fw_arg = arg0 | (arg1<<16);
    int exit_code = 0;

    {
        write_to_non_mmio_device(&fw_arg, sizeof(fw_arg),  core, ARC_RESET_SCRATCH_ADDR + 3 * 4);
    }

    {
        write_to_non_mmio_device(&msg_code, sizeof(fw_arg), core, ARC_RESET_SCRATCH_ADDR + 5 * 4);
    }

    wait_for_non_mmio_flush();
    uint32_t misc = 0;
    read_from_non_mmio_device(&misc, core, ARC_RESET_MISC_CNTL_ADDR, 4);

    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", chip);
        return 1;
    } else {
        misc |= (1 << 16);
        write_to_non_mmio_device(&misc, sizeof(misc), core, ARC_RESET_MISC_CNTL_ADDR);
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto timeout_seconds = std::chrono::seconds(timeout);
        auto start = std::chrono::system_clock::now();
        while (true) {
            if (std::chrono::system_clock::now() - start > timeout_seconds) {
                std::stringstream ss;
                ss << std::hex << msg_code;
                throw std::runtime_error("Timed out after waiting " + std::to_string(timeout) + " seconds for device " + std::to_string(chip) + " ARC to respond to message 0x" +  ss.str());
            }

            uint32_t status = 0;
            read_from_non_mmio_device(&status, core, ARC_RESET_SCRATCH_ADDR + 5 * 4, sizeof(status));
            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    read_from_non_mmio_device(return_3, core, ARC_RESET_SCRATCH_ADDR + 3 * 4, sizeof(uint32_t));
                }

                if (return_4 != nullptr) {
                    read_from_non_mmio_device(return_4, core, ARC_RESET_SCRATCH_ADDR + 4 * 4, sizeof(uint32_t));
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == MSG_ERROR_REPLY) {
                log_warning(LogSiliconDriver, "On device {}, message code 0x{:x} not recognized by FW", chip, msg_code);
                exit_code = MSG_ERROR_REPLY;
                break;
            }
        }
    }
    return exit_code;
}

void tt_SiliconDevice::write_to_sysmem(const void* mem_ptr, std::uint32_t size,  uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
    write_dma_buffer(mem_ptr, size, addr, channel, src_device_id);
}
void tt_SiliconDevice::write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
    write_dma_buffer(vec.data(), vec.size() * sizeof(uint32_t), addr, channel, src_device_id);
}

void tt_SiliconDevice::read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
    read_dma_buffer(mem_ptr, addr, channel, size, src_device_id);
}
void tt_SiliconDevice::read_from_sysmem(std::vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
    size_buffer_to_capacity(vec, size);
    read_dma_buffer(vec.data(), addr, channel, size, src_device_id);
}

void tt_SiliconDevice::set_membar_flag(const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores, const uint32_t barrier_value, const uint32_t barrier_addr, const std::string& fallback_tlb) {
    tt_driver_atomics::sfence(); // Ensure that writes before this do not get reordered
    std::unordered_set<tt_xy_pair> cores_synced = {};
    std::vector<uint32_t> barrier_val_vec = {barrier_value};
    for (const auto& core : cores) {
        write_to_device(barrier_val_vec, tt_cxy_pair(chip, core), barrier_addr, fallback_tlb);
    }
    tt_driver_atomics::sfence(); // Ensure that all writes in the Host WC buffer are flushed
    while (cores_synced.size() != cores.size()) {
        for(const auto& core : cores) {
            if (cores_synced.find(core) == cores_synced.end()) {
                std::vector<uint32_t> readback_vec = {};
                read_from_device(readback_vec, tt_cxy_pair(chip, core), barrier_addr, sizeof(std::uint32_t), fallback_tlb);
                if (readback_vec.at(0) == barrier_value) {
                    cores_synced.insert(core);
                }
                else {
                    log_trace(LogSiliconDriver, "Waiting for core {} to recieve mem bar flag {} in function", core.str(), barrier_value);
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned
    tt_driver_atomics::mfence(); 
}

void tt_SiliconDevice::insert_host_to_device_barrier(const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores, const uint32_t barrier_addr, const std::string& fallback_tlb) {
    // Ensure that this memory barrier is atomic across processes/threads
    const scoped_lock<named_mutex> lock(*get_mutex(MEM_BARRIER_MUTEX_NAME, this->get_pci_device(chip)->id));
    set_membar_flag(chip, cores, tt_MemBarFlag::SET, barrier_addr, fallback_tlb);
    set_membar_flag(chip, cores, tt_MemBarFlag::RESET, barrier_addr, fallback_tlb);
}

void tt_SiliconDevice::init_membars() {
    for(const auto& chip :  target_devices_in_cluster) {
        if (ndesc -> is_chip_mmio_capable(chip)) {
            set_membar_flag(chip, workers_per_chip.at(chip), tt_MemBarFlag::RESET, l1_address_params.TENSIX_L1_BARRIER_BASE, "LARGE_WRITE_TLB");
            set_membar_flag(chip, eth_cores, tt_MemBarFlag::RESET, l1_address_params.ETH_L1_BARRIER_BASE, "LARGE_WRITE_TLB");
            set_membar_flag(chip, dram_cores, tt_MemBarFlag::RESET, dram_address_params.DRAM_BARRIER_BASE, "LARGE_WRITE_TLB");
        }
    }
}
void tt_SiliconDevice::l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {
    if (ndesc -> is_chip_mmio_capable(chip)) {
        const auto& all_workers = workers_per_chip.at(chip);
        const auto& all_eth = eth_cores;
        if (cores.size()) {
            // Insert barrier on specific cores with L1
            std::unordered_set<tt_xy_pair> workers_to_sync = {};
            std::unordered_set<tt_xy_pair> eth_to_sync = {};
            
            for (const auto& core : cores) {
                if (all_workers.find(core) != all_workers.end()) {
                    workers_to_sync.insert(core);
                } else if (all_eth.find(core) != all_eth.end()) {
                    eth_to_sync.insert(core);
                } else {
                    log_fatal("Can only insert an L1 Memory barrier on Tensix or Ethernet cores.");
                }
            }
            insert_host_to_device_barrier(chip, workers_to_sync, l1_address_params.TENSIX_L1_BARRIER_BASE, fallback_tlb);
            insert_host_to_device_barrier(chip, eth_to_sync, l1_address_params.ETH_L1_BARRIER_BASE, fallback_tlb);
        } else {
            // Insert barrier on all cores with L1
            insert_host_to_device_barrier(chip, all_workers, l1_address_params.TENSIX_L1_BARRIER_BASE, fallback_tlb);
            insert_host_to_device_barrier(chip, all_eth, l1_address_params.ETH_L1_BARRIER_BASE, fallback_tlb);
        }
    }
    else {
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {
    if (ndesc -> is_chip_mmio_capable(chip)) {
        if (cores.size()) {
            for(const auto& core : cores) {
                log_assert(dram_cores.find(core) != dram_cores.end(), "Can only insert a DRAM Memory barrier on DRAM cores.");
            }
            insert_host_to_device_barrier(chip, cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
        else {
            // Insert Barrier on all DRAM Cores
            insert_host_to_device_barrier(chip, dram_cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
    }
    else {
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {
    if (ndesc -> is_chip_mmio_capable(chip)) {
        if (channels.size()) {
            std::unordered_set<tt_xy_pair> dram_cores_to_sync = {};
            for(const auto& chan : channels) {
                dram_cores_to_sync.insert(get_soc_descriptor(chip).get_core_for_dram_channel(chan, 0));
            }
            insert_host_to_device_barrier(chip, dram_cores_to_sync, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
        else {
            // Insert Barrier on all DRAM Cores
            insert_host_to_device_barrier(chip, dram_cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
    }
    else {
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::write_to_device(const void *mem_ptr, uint32_t size, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(core.chip);
    if(target_is_mmio_capable) {
        if (fallback_tlb == "REG_TLB") {
            write_mmio_device_register(mem_ptr, core, addr, size, fallback_tlb);
        } else {
            write_device_memory(mem_ptr, size, core, addr, fallback_tlb);
        }
    }
    else if (!send_epoch_cmd) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");
        log_assert((get_soc_descriptor(core.chip).ethernet_cores).size() > 0 && get_number_of_chips_in_cluster() > 1, "Cannot issue ethernet writes to a single chip cluster!");
        write_to_non_mmio_device(mem_ptr, size, core, addr);
    } else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");
        // as long as epoch commands are sent single-threaded, no need to acquire mutex
        log_assert(!(size % 4), "Epoch commands must be 4 byte aligned!");
        write_to_non_mmio_device_send_epoch_cmd((uint32_t*)mem_ptr, size, core, addr, last_send_epoch_cmd, ordered_with_prev_remote_write);
    }
}


void tt_SiliconDevice::write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
    // Overloaded device writer that accepts a vector
    write_to_device(vec.data(), vec.size() * sizeof(uint32_t), core, addr, fallback_tlb, send_epoch_cmd, last_send_epoch_cmd, ordered_with_prev_remote_write);
}


void tt_SiliconDevice::write_epoch_cmd_to_device(const uint32_t *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(core.chip);
    if(target_is_mmio_capable) {
        write_device_memory(mem_ptr, size_in_bytes, core, addr, fallback_tlb);
    } else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");    // MT: Use only dynamic TLBs and never program static
        write_to_non_mmio_device_send_epoch_cmd(mem_ptr, size_in_bytes, core, addr, last_send_epoch_cmd, ordered_with_prev_remote_write);
     }
}

void tt_SiliconDevice::write_epoch_cmd_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
    // Overloaded device writer that accepts a vector
    write_epoch_cmd_to_device(vec.data(), vec.size() * sizeof(uint32_t), core, addr, fallback_tlb, last_send_epoch_cmd, ordered_with_prev_remote_write);
}

void tt_SiliconDevice::rolled_write_to_device(uint32_t* mem_ptr, uint32_t size_in_bytes, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {
    log_assert(!(size_in_bytes % 4), "{} only supports 4-byte aligned data", __FUNCTION__);
    bool target_is_mmio_capable = ndesc->is_chip_mmio_capable(core.chip);

    if (target_is_mmio_capable) {
        for (int i=0; i<unroll_count; i++) {
            *mem_ptr = i; // slot id for debug
            write_device_memory(mem_ptr, size_in_bytes, core, addr + i * size_in_bytes, fallback_tlb);
        }
    }
    else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");    // MT: Use only dynamic TLBs and never program static
        log_assert((get_soc_descriptor(core.chip).ethernet_cores).size() > 0 && get_number_of_chips_in_cluster() > 1, "Cannot issue ethernet writes to a single chip cluster!");
        rolled_write_to_non_mmio_device(mem_ptr, size_in_bytes, core, addr, unroll_count);
    }
}

void tt_SiliconDevice::rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {
    rolled_write_to_device(vec.data(), vec.size() * sizeof(uint32_t), unroll_count, core, addr, fallback_tlb);
}

void tt_SiliconDevice::read_mmio_device_register(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    struct PCIdevice* pci_device = get_pci_device(core.chip);
    TTDevice *dev = pci_device->hdev;

    const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
    const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device -> id));
    LOG1 ("  dynamic tlb_index: %d\n", tlb_index);

    auto [mapped_address, tlb_size] = set_dynamic_tlb(pci_device, tlb_index, core, addr, harvested_coord_translation, TLB_DATA::Strict);
    // Align block to 4bytes if needed. 
    auto aligned_buf = tt_4_byte_aligned_buffer(mem_ptr, size);
    read_regs(dev, mapped_address, aligned_buf.block_size / sizeof(std::uint32_t), aligned_buf.local_storage);

    if(aligned_buf.input_size != aligned_buf.block_size) {
        // Copy value from aligned buffer to main buffer.
        std::memcpy(mem_ptr, aligned_buf.local_storage, size);
    }
}


void tt_SiliconDevice::write_mmio_device_register(const void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    struct PCIdevice* pci_device = get_pci_device(core.chip);
    TTDevice *dev = pci_device->hdev;

    const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
    const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device -> id));
    LOG1 ("  dynamic tlb_index: %d\n", tlb_index);

    auto [mapped_address, tlb_size] = set_dynamic_tlb(pci_device, tlb_index, core, addr, harvested_coord_translation, TLB_DATA::Strict);
    // Align block to 4bytes if needed. 
    auto aligned_buf = tt_4_byte_aligned_buffer(mem_ptr, size);
    if(aligned_buf.input_size != aligned_buf.block_size) {
        // Copy value from main buffer to aligned buffer
        std::memcpy(aligned_buf.local_storage, mem_ptr, size);
    }
    write_regs(dev, mapped_address, aligned_buf.block_size / sizeof(uint32_t), aligned_buf.local_storage);
}

void tt_SiliconDevice::read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(core.chip);
    if (target_is_mmio_capable) {
        if (fallback_tlb == "REG_TLB") {
            read_mmio_device_register(mem_ptr, core, addr, size, fallback_tlb);
        } else {
            read_device_memory(mem_ptr, core, addr, size, fallback_tlb);
        }
    }
    else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");    // MT: Use only dynamic TLBs and never program static
        log_assert((get_soc_descriptor(core.chip).ethernet_cores).size() > 0 &&  get_number_of_chips_in_cluster() > 1, "Cannot issue ethernet reads from a single chip cluster!");
        read_from_non_mmio_device(mem_ptr, core, addr, size);
    }
}

void tt_SiliconDevice::read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    size_buffer_to_capacity(vec, size);
    read_from_device(vec.data(), core, addr, size, fallback_tlb);
}


int tt_SiliconDevice::arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done, uint32_t arg0, uint32_t arg1, int timeout, uint32_t *return_3, uint32_t *return_4) {
    log_assert(arch_name != tt::ARCH::BLACKHOLE, "ARC messages not supported in Blackhole");
    if(ndesc -> is_chip_mmio_capable(logical_device_id)) {
        return pcie_arc_msg(logical_device_id, msg_code, wait_for_done, arg0, arg1, timeout, return_3, return_4);
    }
    else {
        return remote_arc_msg(logical_device_id, msg_code, wait_for_done, arg0, arg1, timeout, return_3, return_4);
    }
}

void tt_SiliconDevice::send_tensix_risc_reset_to_core(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets) {
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type) valid;
    write_to_device(&valid_val, sizeof(uint32_t), core, 0xFFB121B0, "REG_TLB");
    tt_driver_atomics::sfence();
}

void tt_SiliconDevice::send_remote_tensix_risc_reset_to_core(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets) {
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type) valid;
    write_to_non_mmio_device(&valid_val, sizeof(uint32_t), core, 0xFFB121B0);
    tt_driver_atomics::sfence();
}

int tt_SiliconDevice::set_remote_power_state(const chip_id_t &chip, tt_DevicePowerState device_state) {
    auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(chip);
    struct PCIdevice* pci_device = get_pci_device(mmio_capable_chip_logical);
    return remote_arc_msg(chip, get_power_state_arc_msg(pci_device, device_state), true, 0, 0, 1, NULL, NULL);
}


void tt_SiliconDevice::enable_remote_ethernet_queue(const chip_id_t& chip, int timeout) {
    uint32_t msg_success = 0x0;
    auto timeout_seconds = std::chrono::seconds(timeout);
    auto start = std::chrono::system_clock::now();
    while (msg_success != 1) {
        if (std::chrono::system_clock::now() - start > timeout_seconds) {
            throw std::runtime_error("Timed out after waiting " + std::to_string(timeout) + " seconds for DRAM to finish training");
        }
        int msg_rt = remote_arc_msg(chip, 0xaa58, true, 0xFFFF, 0xFFFF, 1, &msg_success, NULL);
        if (msg_rt == MSG_ERROR_REPLY) {
            break;
        }
    }
}


void tt_SiliconDevice::broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions &soft_resets) {
    if(arch_name == tt::ARCH::GRAYSKULL) {
        for (auto &device_it : m_pci_device_map) {
            broadcast_pcie_tensix_risc_reset(device_it.second, soft_resets);
        }
    }
    else {
        auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
        uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type) valid;
        std::set<chip_id_t> chips_to_exclude = {};
        std::set<uint32_t> rows_to_exclude;
        std::set<uint32_t> columns_to_exclude;
        if (arch_name == tt::ARCH::BLACKHOLE) {
            rows_to_exclude = {0, 1};
            columns_to_exclude = {0, 8, 9};
        } else {
            rows_to_exclude = {0, 6};
            columns_to_exclude = {0, 5};
        }
        std::string fallback_tlb = "LARGE_WRITE_TLB";
        broadcast_write_to_cluster(&valid_val, sizeof(uint32_t), 0xFFB121B0, chips_to_exclude, rows_to_exclude, columns_to_exclude, fallback_tlb);
        // Ensure that reset signal is globally visible
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::set_power_state(tt_DevicePowerState device_state) {
    // MT Initial BH - ARC messages not supported in Blackhole
    if (arch_name != tt::ARCH::BLACKHOLE) {
        for(auto& chip : target_devices_in_cluster) {
            if(ndesc -> is_chip_mmio_capable(chip)) {
                set_pcie_power_state(device_state);
            } else {
                int exit_code = set_remote_power_state(chip, device_state);
                log_assert(exit_code == 0, "Failed to set power state to {} with exit code: {}", device_state, exit_code);
            }
        }
    }
}

void tt_SiliconDevice::enable_ethernet_queue(int timeout) {
    for (const chip_id_t &chip : target_devices_in_cluster) {
        auto arch = get_soc_descriptor(chip).arch;

         switch (arch) {
            case tt::ARCH::WORMHOLE:
            case tt::ARCH::WORMHOLE_B0: {
                if (ndesc->is_chip_mmio_capable(chip)) {
                    enable_local_ethernet_queue(chip, timeout);
                } else {
                    enable_remote_ethernet_queue(chip, timeout);
                }

                break;
            case tt::ARCH::BLACKHOLE:
                log_assert(false, "Arch BLACKHOLE doesn't support ethernet queues yet");
            }
            default: {
                break;
            }
        }

    }
}

std::set<chip_id_t> tt_SiliconDevice::get_target_remote_device_ids() {
    return target_remote_chips;
}

void tt_SiliconDevice::deassert_resets_and_set_power_state() {
    // Assert tensix resets on all chips in cluster
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);

    // MT Initial BH - ARC messages not supported in Blackhole
    if (arch_name != tt::ARCH::BLACKHOLE) {
        // Send ARC Messages to deassert RISCV resets
        for (auto &device_it : m_pci_device_map){
            arc_msg(device_it.first, 0xaa00 | device_it.second->hdev->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(), true, 0, 0);
        }
        if(ndesc != nullptr) {
            for(const chip_id_t& chip : target_devices_in_cluster) {
                if(!ndesc -> is_chip_mmio_capable(chip)) {
                    auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(chip);
                    struct PCIdevice* pci_device = get_pci_device(mmio_capable_chip_logical);
                    remote_arc_msg(chip, 0xaa00 | pci_device->hdev->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(), true, 0x0, 0x0, 1, NULL, NULL);
                }
            }
            enable_ethernet_queue(30);
        }
        // Set power state to busy
        set_power_state(tt_DevicePowerState::BUSY);
    }
}

void tt_SiliconDevice::verify_eth_fw() {
    for(const auto& chip : target_devices_in_cluster) {
        std::vector<uint32_t> mem_vector;
        std::vector<uint32_t> fw_versions;
        for (tt_xy_pair &eth_core : get_soc_descriptor(chip).ethernet_cores) {
            read_from_device(mem_vector, tt_cxy_pair(chip, eth_core), l1_address_params.FW_VERSION_ADDR, sizeof(uint32_t), "LARGE_READ_TLB");
            fw_versions.push_back(mem_vector.at(0));
        }
        verify_sw_fw_versions(chip, SW_VERSION, fw_versions);
        eth_fw_version = tt_version(fw_versions.at(0));
    }
}

void tt_SiliconDevice::verify_sw_fw_versions(int device_id, std::uint32_t sw_version, std::vector<std::uint32_t> &fw_versions) {
    tt_version sw(sw_version), fw_first_eth_core(fw_versions.at(0));
    log_info(
        LogSiliconDriver,
        "Software version {}, Ethernet FW version {} (Device {})",
        sw.str(),
        fw_first_eth_core.str(),
        device_id);
    for (std::uint32_t &fw_version : fw_versions) {
        tt_version fw(fw_version);
        log_assert(fw == fw_first_eth_core, "FW versions are not the same across different ethernet cores");
        log_assert(sw.major == fw.major, "SW/FW major version number out of sync");
        log_assert(sw.minor <= fw.minor, "SW version is newer than FW version");
    }

    // Min ERISC FW version required to support ordered writes is 6.4.0
    use_ethernet_ordered_writes &= fw_first_eth_core >= tt_version(6, 4, 0);
    // Min ERISC FW version required to support ethernet broadcast is 6.5.0.
    use_ethernet_broadcast &= fw_first_eth_core >= tt_version(6, 5, 0);
    // Virtual coordinates can be used for broadcast headers if ERISC FW >= 6.8.0 and NOC translation is enabled
    // Temporarily enable this feature for 6.7.241 as well for testing.
    use_virtual_coords_for_eth_broadcast &= (fw_first_eth_core >= tt_version(6, 8, 0) || fw_first_eth_core == tt_version(6, 7, 241)) && translation_tables_en;
}

void tt_SiliconDevice::start_device(const tt_device_params &device_params) {
    if(device_params.init_device) {
        initialize_pcie_devices();
        // MT Initial BH - Ethernet firmware not present in Blackhole
        if(arch_name == tt::ARCH::WORMHOLE || arch_name == tt::ARCH::WORMHOLE_B0) {
            verify_eth_fw();
        }
        deassert_resets_and_set_power_state();
    }
}

void tt_SiliconDevice::close_device() {
    set_power_state(tt_DevicePowerState::LONG_IDLE);
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);
}


void tt_SiliconDevice::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
    l1_address_params = l1_address_params_;
}

void tt_SiliconDevice::set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {
    dram_address_params = dram_address_params_;
}

void tt_SiliconDevice::set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_) {
    host_address_params = host_address_params_;
}

void tt_SiliconDevice::set_driver_eth_interface_params(const tt_driver_eth_interface_params& eth_interface_params_) {
    eth_interface_params = eth_interface_params_;
}

void tt_SiliconDevice::setup_core_to_tlb_map(std::function<std::int32_t(tt_xy_pair)> mapping_function) {
    map_core_to_tlb = mapping_function;
    tlbs_init = true;
}

std::uint32_t tt_SiliconDevice::get_num_dram_channels(std::uint32_t device_id) {
    log_assert(target_devices_in_cluster.find(device_id) != target_devices_in_cluster.end(), "Querying DRAM parameters for a device that does not exist.");
    return get_soc_descriptor(device_id).get_num_dram_channels();
}

std::uint32_t tt_SiliconDevice::get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    log_assert(channel < get_num_dram_channels(device_id), "Querying size for a device channel that does not exist.");
    return  get_soc_descriptor(device_id).dram_bank_size; // Space per channel is identical for now
}

std::uint32_t tt_SiliconDevice::get_num_host_channels(std::uint32_t device_id) {
    log_assert(all_target_mmio_devices.find(device_id) != all_target_mmio_devices.end(), "Querying Host Address parameters for a non-mmio device or a device does not exist.");
    return m_num_host_mem_channels; // Same number of host channels per device for now
}

std::uint32_t tt_SiliconDevice::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    log_assert(host_channel_size.size(), "Host channel size can only be queried after the device has been started.");
    log_assert(channel < get_num_host_channels(device_id), "Querying size for a host channel that does not exist.");
    return host_channel_size.at(device_id).at(channel);
}

std::uint32_t tt_SiliconDevice::get_pcie_speed(std::uint32_t device_id) {
    int link_width = 0;
    int link_speed = 0;
    if (ndesc->is_chip_mmio_capable(device_id)) {
        PCIdevice *pci_device = get_pci_device(device_id);
        link_width = get_link_width(pci_device->hdev);
        link_speed = get_link_speed(pci_device->hdev);
        log_debug(LogSiliconDriver, "Device {} PCIe link width: x{}, speed: {} Gb/s", device_id, link_width, link_speed);
    } else {
        log_debug(LogSiliconDriver, "Device {} is NOT a PCIe device, width: x{}, speed: {} Gb/s", device_id, link_width, link_speed);
    }
    return (link_width * link_speed);
}

std::uint64_t tt_SiliconDevice::get_pcie_base_addr_from_device() const {
    if(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
        return 0x800000000;
    }
    else {
        return 0;
    }
}

tt_version tt_SiliconDevice::get_ethernet_fw_version() const {
    log_assert(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0, "Can only get Ethernet FW version for Wormhole architectures.");
    log_assert(eth_fw_version.major != 0xffff and eth_fw_version.minor != 0xff and eth_fw_version.patch != 0xff, "Device must be started before querying Ethernet FW version.");
    return eth_fw_version;
}
