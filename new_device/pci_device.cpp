#include "pci_device.h"

#include "system_util.h"
#include "common/logger.hpp"
#include "driver_atomics.h"

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <cstdarg>

#include <sys/mman.h>
#include <unistd.h>
#include <linux/pci.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <wait.h>


// Print all buffers smaller than this number of bytes
constexpr uint32_t g_NUM_BYTES_TO_PRINT = 8;
#define RST "\e[0m"
#define LOG1(...) clr_printf("", __VA_ARGS__)  // Mostly debugging
#define LOG2(...) clr_printf("", __VA_ARGS__)  // Mostly debugging

inline void clr_printf(const char *clr, const char *fmt, ...) {
    va_list args; va_start(args, fmt); printf ("%s", clr); vprintf(fmt, args); printf (RST); va_end(args);
}


namespace tt::umd {


static uint32_t GS_BAR0_WC_MAPPING_SIZE = (156<<20) + (10<<21) + (18<<24);
static uint32_t BH_BAR0_WC_MAPPING_SIZE = 188<<21; // Defines the address for WC region. addresses 0 to BH_BAR0_WC_MAPPING_SIZE are in WC, above that are UC

static const uint32_t GS_WH_ARC_SCRATCH_6_OFFSET = 0x1FF30078;
static const uint32_t BH_NOC_NODE_ID_OFFSET = 0x1FD04044;

inline void record_access (const char* where, uint32_t addr, uint32_t size, bool turbo, bool write, bool block, bool endline) {
    LOG2 ("%s PCI_ACCESS %s 0x%8x  %8d bytes %s %s%s", where, write ? "WR" : "RD", addr, size, turbo ? "TU" : "  ", block ? "BLK" : "   ", endline ? "\n" : "" );
}



inline void print_buffer (const void* buffer_addr, uint32_t len_bytes = 16, bool endline = true) {
    // Prints each byte in a buffer
    {
        uint8_t *b = (uint8_t *)(buffer_addr);
        for (uint32_t i = 0; i < len_bytes; i++) {
            LOG2 ("    [0x%x] = 0x%x (%u) ", i, b[i], b[i]);
        }
        if (endline) {
            LOG2 ("\n");
        }
    }
}

// brosko: abolish this
inline void write_regs_nonclass(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

bool is_grayskull(const uint16_t device_id) { return device_id == 0xfaca; }

bool is_wormhole(const uint16_t device_id) { return device_id == 0x401e; }

bool is_blackhole(const uint16_t device_id) { return device_id == 0xb140; }

bool is_grayskull(const tenstorrent_get_device_info_out &device_info) { return is_grayskull(device_info.device_id); }

bool is_wormhole(const tenstorrent_get_device_info_out &device_info) { return is_wormhole(device_info.device_id); }

bool is_wormhole_b0(const uint16_t device_id, const uint16_t revision_id) {
    return (is_wormhole(device_id) && (revision_id == 0x01));
}

bool is_blackhole(const tenstorrent_get_device_info_out &device_info) { return is_blackhole(device_info.device_id); }

PCIDevice::~PCIDevice() { reset(); }

PCIDevice::PCIDevice(PCIDevice &&that) :
    PCIDeviceBase(std::move(that)) {
    that.drop();
}

PCIDevice& PCIDevice::operator=(PCIDevice &&that) {
    reset();

    *static_cast<PCIDeviceBase *>(this) = std::move(that);
    arch = that.arch;
    that.drop();

    return *this;
}

void PCIDevice::suspend_before_device_reset() { reset(); }

void PCIDevice::resume_after_device_reset() { do_open(); }

Arch PCIDevice::get_arch() const { return arch; }

void PCIDevice::reset() {
    if (device_fd != -1) {
        close(device_fd);
    }

    if (bar0_wc != nullptr && bar0_wc != MAP_FAILED && bar0_wc != bar0_uc) {
        munmap(bar0_wc, bar0_wc_size);
    }

    if (bar0_uc != nullptr && bar0_uc != MAP_FAILED) {
        munmap(bar0_uc, bar0_uc_size);
    }

    if (bar2_uc != nullptr && bar2_uc != MAP_FAILED) {
        munmap(bar2_uc, bar2_uc_size);
    }

    if (bar4_wc != nullptr && bar4_wc != MAP_FAILED) {
        munmap(bar4_wc, bar4_wc_size);
    }

    if (system_reg_mapping != nullptr && system_reg_mapping != MAP_FAILED) {
        munmap(system_reg_mapping, system_reg_mapping_size);
    }

    if (sysfs_config_fd != -1) {
        close(sysfs_config_fd);
    }

    drop();
}

void PCIDevice::drop() {
    device_fd = -1;
    bar0_uc = nullptr;
    bar0_wc = nullptr;
    bar2_uc = nullptr;
    bar4_wc = nullptr;
    system_reg_mapping = nullptr;
    sysfs_config_fd = -1;
}

PCIDevice::PCIDevice(unsigned int device_id) {
    static int unique_id = 0;
    index = device_id;
    do_open();

    // brosko: logical_id is set using information from tt_ClusterDescriptor:: get_chips_with_mmio
    // see if this is still needed for remote chip to work or if it is redundant
    logical_id = device_id;
}

void PCIDevice::do_open() {
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

    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;

    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(device_fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(std::string("Query mappings failed on device ") + std::to_string(index) + ".");
    }

    // Mapping resource to BAR
    // Resource 0 -> BAR0
    // Resource 1 -> BAR2
    // Resource 2 -> BAR4
    tenstorrent_mapping bar0_uc_mapping;
    tenstorrent_mapping bar0_wc_mapping;
    tenstorrent_mapping bar2_uc_mapping;
    tenstorrent_mapping bar2_wc_mapping;
    tenstorrent_mapping bar4_uc_mapping;
    tenstorrent_mapping bar4_wc_mapping;

    memset(&bar0_uc_mapping, 0, sizeof(bar0_uc_mapping));
    memset(&bar0_wc_mapping, 0, sizeof(bar0_wc_mapping));
    memset(&bar2_uc_mapping, 0, sizeof(bar2_uc_mapping));
    memset(&bar2_wc_mapping, 0, sizeof(bar2_wc_mapping));
    memset(&bar4_uc_mapping, 0, sizeof(bar4_uc_mapping));
    memset(&bar4_wc_mapping, 0, sizeof(bar4_wc_mapping));

    for (unsigned int i = 0; i < mappings.query_mappings.in.output_mapping_count; i++) {
        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_UC) {
            bar0_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
            bar0_wc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE1_UC) {
            bar2_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE1_WC) {
            bar2_wc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_UC) {
            bar4_uc_mapping = mappings.mapping_array[i];
        }

        if (mappings.mapping_array[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE2_WC) {
            bar4_wc_mapping = mappings.mapping_array[i];
        }

        log_debug(
            LogSiliconDriver,
            "BAR mapping id {} base {} size {}",
            mappings.mapping_array[i].mapping_id,
            (void *)mappings.mapping_array[i].mapping_base,
            mappings.mapping_array[i].mapping_size);
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

    bar0_uc = mmap(
        NULL,
        bar0_uc_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        device_fd,
        bar0_uc_mapping.mapping_base + bar0_uc_offset);

    if (bar0_uc == MAP_FAILED) {
        throw std::runtime_error(
            std::string("BAR0 UC memory mapping failed for device ") + std::to_string(index) + ".");
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (is_wormhole(device_info.out)) {
        if (bar4_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR4 UC mapping.");
        }

        this->system_reg_mapping_size = bar4_uc_mapping.mapping_size;

        this->system_reg_mapping = mmap(
            NULL,
            bar4_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            device_fd,
            bar4_uc_mapping.mapping_base);

        if (this->system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(
                std::string("BAR4 UC memory mapping failed for device ") + std::to_string(index) + ".");
        }

        this->system_reg_start_offset = (512 - 16) * 1024 * 1024;
        this->system_reg_offset_adjust = (512 - 32) * 1024 * 1024;
    } else if (is_blackhole(device_info.out)) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE1_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR2 UC mapping.");
        }

        // Using UnCachable memory mode. This is used for accessing registers on Blackhole.
        this->bar2_uc_size = bar2_uc_mapping.mapping_size;
        this->bar2_uc = mmap(
            NULL,
            bar2_uc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            device_fd,
            bar2_uc_mapping.mapping_base);

        if (this->bar2_uc == MAP_FAILED) {
            throw std::runtime_error(
                std::string("BAR2 UC memory mapping failed for device ") + std::to_string(index) + ".");
        }

        if (bar4_wc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_WC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(index) + " has no BAR4 WC mapping.");
        }

        // Using Write-Combine memory mode. This is used for accessing DRAM on Blackhole.
        // WC doesn't guarantee write ordering but has better performance.
        this->bar4_wc_size = bar4_wc_mapping.mapping_size;
        this->bar4_wc = mmap(
            NULL,
            bar4_wc_mapping.mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            device_fd,
            bar4_wc_mapping.mapping_base);

        if (this->bar4_wc == MAP_FAILED) {
            throw std::runtime_error(
                std::string("BAR4 WC memory mapping failed for device ") + std::to_string(index) + ".");
        }
    }
    pci_domain = device_info.out.pci_domain;
    pci_bus = device_info.out.bus_dev_fn >> 8;
    pci_device = PCI_SLOT(device_info.out.bus_dev_fn);
    pci_function = PCI_FUNC(device_info.out.bus_dev_fn);

    arch = detect_arch();

    // GS+WH: ARC_SCRATCH[6], BH: NOC NODE_ID
    this->read_checking_offset = is_blackhole(device_info.out) ? BH_NOC_NODE_ID_OFFSET : GS_WH_ARC_SCRATCH_6_OFFSET;
}

Arch PCIDevice::detect_arch_from_device_id(unsigned int device_id) {

    Arch arch_name = Arch::Invalid;
    if (find_device(device_id) == -1) {
        // WARN("---- tt_SiliconDevice::detect_arch did not find silcon device_id: %d\n", device_id);
        return arch_name;
    }
    PCIDevice pci_device (device_id);

    arch_name = pci_device.detect_arch();

    return arch_name;
}


// Open a unique device_id per host memory channel (workaround for ttkmd < 1.21 support for more than 1 pin per fd)
void PCIDevice::open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels) {
    for (int ch = 0; ch < num_host_mem_channels; ch++) {
        log_debug(
            LogSiliconDriver,
            "Opening device_fd_per_host_ch device index: {} ch: {} (num_host_mem_channels: {})",
            index,
            ch,
            num_host_mem_channels);
        int device_fd_for_host_mem = find_device(index);
        if (device_fd_for_host_mem == -1) {
            throw std::runtime_error(
                std::string("Failed opening a host memory device handle for device ") + std::to_string(index));
        }
        device_fd_per_host_ch.push_back(device_fd_for_host_mem);
    }
}

int PCIDevice::get_config_space_fd() {
    if (sysfs_config_fd == -1) {
        static const char pattern[] = "/sys/bus/pci/devices/0000:%02x:%02x.%u/config";
        char buf[sizeof(pattern)];
        std::snprintf(
            buf,
            sizeof(buf),
            pattern,
            (unsigned int)pci_bus,
            (unsigned int)pci_device,
            (unsigned int)pci_function);
        sysfs_config_fd = open(buf, O_RDWR);

        if (sysfs_config_fd == -1) {
            sysfs_config_fd = open(buf, O_RDONLY);
        }
    }

    return sysfs_config_fd;
}

// brosko: consider caching this info
int PCIDevice::get_revision_id() {
    static const char pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/revision";
    char buf[sizeof(pattern)];
    std::snprintf(
        buf,
        sizeof(buf),
        pattern,
        (unsigned int)pci_domain,
        (unsigned int)pci_bus,
        (unsigned int)pci_device,
        (unsigned int)pci_function);

    std::ifstream revision_file(buf);
    std::string revision_string;
    if (std::getline(revision_file, revision_string)) {
        return std::stoi(revision_string, nullptr, 0);
    } else {
        throw std::runtime_error("Revision ID read failed for device");
    }
}

Arch PCIDevice::detect_arch() {
    if (is_grayskull(device_info.device_id)) {
        return Arch::GRAYSKULL;
    } else if (is_wormhole_b0(device_info.device_id, get_revision_id())) {
        return Arch::WORMHOLE_B0;
    } else if (is_wormhole(device_info.device_id)) {
        return Arch::WORMHOLE;
    } else if (is_blackhole(device_info.device_id)) {
        return Arch::BLACKHOLE;
    } else {
        throw std::runtime_error(std::string("Unknown device id."));
    }
}


int PCIDevice::get_numa_node() {
    static const char pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/numa_node";
    char buf[sizeof(pattern)];
    std::snprintf(
        buf,
        sizeof(buf),
        pattern,
        (unsigned int)pci_domain,
        (unsigned int)pci_bus,
        (unsigned int)pci_device,
        (unsigned int)pci_function);

    std::ifstream num_node_file(buf);
    std::string numa_node_string;
    if (std::getline(num_node_file, numa_node_string)) {
        return std::stoi(numa_node_string, nullptr, 0);
    } else {
        return -1;
    }
}

std::uint64_t PCIDevice::read_bar0_base() {
    const std::uint64_t bar_address_mask = ~(std::uint64_t)0xF;
    unsigned int bar0_config_offset = 0x10;

    std::uint64_t bar01;
    if (pread(get_config_space_fd(), &bar01, sizeof(bar01), bar0_config_offset) != sizeof(bar01)) {
        return 0;
    }

    return bar01 & bar_address_mask;
}

bool PCIDevice::reset_by_sysfs() {
    const char *virtual_env = getenv("VIRTUAL_ENV");
    if (virtual_env == nullptr)
        return false;

    std::string reset_helper_path = virtual_env;
    reset_helper_path += "/bin/reset-helper";

    std::string busid = std::to_string(pci_bus);

    suspend_before_device_reset();

    char *argv[3];
    argv[0] = const_cast<char *>(reset_helper_path.c_str());
    argv[1] = const_cast<char *>(busid.c_str());
    argv[2] = nullptr;

    pid_t reset_helper_pid;
    if (posix_spawn(&reset_helper_pid, reset_helper_path.c_str(), nullptr, nullptr, argv, environ) != 0)
        return false;

    siginfo_t reset_helper_status;
    if (waitid(P_PID, reset_helper_pid, &reset_helper_status, WEXITED) != 0)
        return false;

    if (reset_helper_status.si_status != 0)
        return false;

    resume_after_device_reset();

    return true;
}

bool PCIDevice::reset_by_ioctl() {
    struct tenstorrent_reset_device reset_device;
    memset(&reset_device, 0, sizeof(reset_device));

    reset_device.in.output_size_bytes = sizeof(reset_device.out);
    reset_device.in.flags = 0;

    if (ioctl(device_fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset_device) == -1) {
        return false;
    }

    return (reset_device.out.result == 0);
}

void PCIDevice::write_regs(uint32_t byte_addr, uint32_t word_len, const void *data) {
    record_access("write_regs", byte_addr, word_len * sizeof(uint32_t), false, true, false, false);

    volatile uint32_t *dest = register_address<std::uint32_t>(byte_addr);
    const uint32_t *src = reinterpret_cast<const uint32_t *>(data);

    write_regs_nonclass(dest, src, word_len);

    LOG2(" REG ");
    print_buffer(data, std::min(g_NUM_BYTES_TO_PRINT, word_len * 4), true);
}

void PCIDevice::write_tlb_reg(
    uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size) {
    record_access("write_tlb_reg", byte_addr, tlb_cfg_reg_size, false, true, false, false);

    log_assert(
        (tlb_cfg_reg_size == 8) or (tlb_cfg_reg_size == 12),
        "Tenstorrent hardware supports only 64bit or 96bit TLB config regs");

    volatile uint64_t *dest_qw = register_address<std::uint64_t>(byte_addr);
    volatile uint32_t *dest_extra_dw = register_address<std::uint32_t>(byte_addr + 8);
#if defined(__ARM_ARCH) || defined(__riscv)
    // The store below goes through UC memory on x86, which has implicit ordering constraints with WC accesses.
    // ARM has no concept of UC memory. This will not allow for implicit ordering of this store wrt other memory
    // accesses. Insert an explicit full memory barrier for ARM. Do the same for RISC-V.
    tt_driver_atomics::mfence();
#endif
    *dest_qw = value_lower;
    if (tlb_cfg_reg_size > 8) {
        uint32_t *p_value_upper = reinterpret_cast<uint32_t *>(&value_upper);
        *dest_extra_dw = p_value_upper[0];
    }
    tt_driver_atomics::mfence();  // Otherwise subsequent WC loads move earlier than the above UC store to the TLB
                                  // register.

    LOG2(" TLB ");
    print_buffer(&value_lower, sizeof(value_lower), true);
    if (tlb_cfg_reg_size > 8) {
        uint32_t *p_value_upper = reinterpret_cast<uint32_t *>(&value_upper);
        print_buffer(p_value_upper, sizeof(uint32_t), true);
    }
}

void PCIDevice::read_regs(uint32_t byte_addr, uint32_t word_len, void *data) {
    record_access("read_regs", byte_addr, word_len * sizeof(uint32_t), false, false, false, false);

    const volatile uint32_t *src = register_address<std::uint32_t>(byte_addr);
    uint32_t *dest = reinterpret_cast<uint32_t *>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
    LOG2(" REG ");
    print_buffer(data, std::min(g_NUM_BYTES_TO_PRINT, word_len * 4), true);
}

}  // namespace tt::umd