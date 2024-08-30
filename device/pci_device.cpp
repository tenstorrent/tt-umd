/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <vector>
#include <fcntl.h>  // for ::open
#include <unistd.h> // for ::close
#include <sys/ioctl.h> // for ioctl
#include <sys/mman.h>  // for mmap, munmap
#include <linux/pci.h> // for PCI_SLOT, PCI_FUNC

#include "pci_device.hpp"
#include "architecture_implementation.h"
#include "ioctl.h"
#include "device/tt_arch_types.h"
#include "device/driver_atomics.h"

#include "common/assert.hpp"
#include "common/logger.hpp"

int find_device(const uint16_t device_id) {
    // returns device id if found, otherwise -1
    const char device_name_pattern [] = "/dev/tenstorrent/%u";
    char device_name[sizeof(device_name_pattern) + std::numeric_limits<unsigned int>::digits10];
    std::snprintf(device_name, sizeof(device_name), device_name_pattern, (unsigned int)device_id);
    int device_fd = ::open(device_name, O_RDWR | O_CLOEXEC);
    // LOG2 ("find_device() open call returns device_fd: %d for device_name: %s (device_id: %d)\n", device_fd, device_name, device_id);
    return device_fd;
}

tt::ARCH detect_arch(uint16_t pcie_device_id, int pcie_revision_id) {
    if (pcie_device_id == 0xfaca){
        return tt::ARCH::GRAYSKULL;
    } else if (pcie_device_id == 0x401e && pcie_revision_id == 0x01){
        return tt::ARCH::WORMHOLE_B0;
    } else if (pcie_device_id == 0x401e){
        TT_THROW("Wormhole is not supported. Please use Wormhole B0 instead.");
        return tt::ARCH::WORMHOLE;
    } else if (pcie_device_id == 0xb140){
        return tt::ARCH::BLACKHOLE;
    } else {
        TT_THROW("Unknown pcie device id that does not match any known architecture: ", pcie_device_id);
    }
}

// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------

TTDevice::TTDevice(int device_id, int logical_device_id){
    this->device_id = device_id;
    this->logical_id = logical_device_id;
    setup_device();
}

TTDevice::~TTDevice(){
    close_device();
}

void TTDevice::setup_device() {
    device_fd = find_device(device_id);
    get_pcie_info();
    if (device_fd == -1) {
        throw std::runtime_error(std::string("Failed opening a handle for device ") + std::to_string(device_id));
    }

    arch = detect_arch(pcie_device_id, pcie_revision_id);
    architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch));

    // Get PCIe device info through IOTCL -> tt-kmd
    tenstorrent_get_device_info device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.in.output_size_bytes = sizeof(device_info.out);
    if (ioctl(device_fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &device_info) == -1) {
        throw std::runtime_error(std::string("Get device info failed on device ") + std::to_string(device_id) + ".");
    }

    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;

    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(device_fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(std::string("Query mappings failed on device ") + std::to_string(device_id) + ".");
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

        log_debug(LogSiliconDriver, "BAR mapping id {} base {} size {}",
            mappings.mapping_array[i].mapping_id,
            (void *)mappings.mapping_array[i].mapping_base,
            mappings.mapping_array[i].mapping_size);
    }

    if (bar0_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE0_UC) {
        throw std::runtime_error(std::string("Device ") + std::to_string(device_id) + " has no BAR0 UC mapping.");
    }

    auto wc_mapping_size = arch == tt::ARCH::BLACKHOLE ? BH_BAR0_WC_MAPPING_SIZE : GS_BAR0_WC_MAPPING_SIZE;

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
        throw std::runtime_error(std::string("BAR0 UC memory mapping failed for device ") + std::to_string(device_id) + ".");
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (arch == tt::ARCH::WORMHOLE_B0) {
        if (bar4_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(device_id) + " has no BAR4 UC mapping.");
        }

        system_reg_mapping_size = bar4_uc_mapping.mapping_size;

        system_reg_mapping = mmap(NULL, bar4_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar4_uc_mapping.mapping_base);

        if (system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(std::string("BAR4 UC memory mapping failed for device ") + std::to_string(device_id) + ".");
        }

        system_reg_start_offset = (512 - 16) * 1024*1024;
        system_reg_offset_adjust = (512 - 32) * 1024*1024;
    } else if(arch == tt::ARCH::BLACKHOLE) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE1_UC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(device_id) + " has no BAR2 UC mapping.");
        }

        // Using UnCachable memory mode. This is used for accessing registers on Blackhole.
        bar2_uc_size = bar2_uc_mapping.mapping_size;
        bar2_uc = mmap(NULL, bar2_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar2_uc_mapping.mapping_base);

        if (bar2_uc == MAP_FAILED) {
            throw std::runtime_error(std::string("BAR2 UC memory mapping failed for device ") + std::to_string(device_id) + ".");
        }

        if (bar4_wc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_WC) {
            throw std::runtime_error(std::string("Device ") + std::to_string(device_id) + " has no BAR4 WC mapping.");
        }

        // Using Write-Combine memory mode. This is used for accessing DRAM on Blackhole.
        // WC doesn't guarantee write ordering but has better performance.
        bar4_wc_size = bar4_wc_mapping.mapping_size;
        bar4_wc = mmap(NULL, bar4_wc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar4_wc_mapping.mapping_base);

        if (bar4_wc == MAP_FAILED) {
            throw std::runtime_error(std::string("BAR4 WC memory mapping failed for device ") + std::to_string(device_id) + ".");
        }
    }

    // GS+WH: ARC_SCRATCH[6], BH: NOC NODE_ID
    read_checking_offset = arch == tt::ARCH::BLACKHOLE ? BH_NOC_NODE_ID_OFFSET : GS_WH_ARC_SCRATCH_6_OFFSET;
}

void TTDevice::close_device() {
    if (arch == tt::ARCH::BLACKHOLE && bar2_uc != nullptr && bar2_uc != MAP_FAILED) {
        // Disable ATU index 0
        // TODO: Implement disabling for all indexes, once more host channels are enabled.
        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;
        uint32_t region_ctrl_2 = 0 << 31; // REGION_EN = 0
        write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(bar2_uc) + iatu_base + 0x04), &region_ctrl_2, 1);
    }

    if (device_fd != -1) {
        ::close(device_fd);
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

    device_fd = -1;
    bar0_uc = nullptr;
    bar0_wc = nullptr;
    bar2_uc = nullptr;
    bar4_wc = nullptr;
    system_reg_mapping = nullptr;
}

void TTDevice::get_pcie_info() {
    // Get PCIe device info through IOTCL -> tt-kmd and return pci_device_id and revision_id
    std::uint16_t pcie_domain;
    std::uint8_t pcie_bus;
    std::uint8_t pcie_device;
    std::uint8_t pcie_function;

    tenstorrent_get_device_info device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.in.output_size_bytes = sizeof(device_info.out);
    if (ioctl(this->device_fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &device_info) == -1) {
        TT_THROW("Get PCIe device info failed on device: ", this->device_id);
    }
    pcie_domain = device_info.out.pci_domain;
    pcie_bus = device_info.out.bus_dev_fn >> 8;
    pcie_device = PCI_SLOT(device_info.out.bus_dev_fn);
    pcie_function = PCI_FUNC(device_info.out.bus_dev_fn);

    // Get the PCIe revision ID from sysfs
    static const char sys_pattern[] = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/%s";
    char buf[sizeof(sys_pattern) + 10];

    // revision pattern = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/revision"
    std::snprintf(buf, sizeof(buf), sys_pattern, pcie_domain, pcie_bus, pcie_device, pcie_function, "revision");

    std::ifstream revision_file(buf);
    std::string revision_string;
    if (std::getline(revision_file, revision_string)) {
        this->pcie_device_id = device_info.out.device_id;
        this->pcie_revision_id = std::stoi(revision_string, nullptr, 0);
    } else {
        TT_THROW("Revision ID /sys/ read failed for device: ", this->device_id);
    }

    // Get NUMA node from sysfs
    // numa node pattern = "/sys/bus/pci/devices/%04x:%02x:%02x.%u/numa_node"
    std::snprintf(buf, sizeof(buf), sys_pattern, pcie_domain, pcie_bus, pcie_device, pcie_function, "numa_node");

    std::ifstream num_node_file(buf);
    std::string numa_node_string;
    if (std::getline(num_node_file, numa_node_string)) {
        this->numa_node = std::stoi(numa_node_string, nullptr, 0);
    } else {
        TT_THROW("Numa node /sys/ read failed for device: ", this->device_id);
    }
}

// Open a unique device_id per host memory channel (workaround for ttkmd < 1.21 support for more than 1 pin per fd)
void TTDevice::open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels) {
    for (int ch = 0; ch < num_host_mem_channels; ch++) {
        log_debug(LogSiliconDriver, "Opening device_fd_per_host_ch device index: {} ch: {} (num_host_mem_channels: {})", device_id, ch, num_host_mem_channels);
        int device_fd_for_host_mem = find_device(device_id);
        if (device_fd_for_host_mem == -1) {
            throw std::runtime_error(std::string("Failed opening a host memory device handle for device ") + std::to_string(device_id));
        }
        device_fd_per_host_ch.push_back(device_fd_for_host_mem);
    }
}

tt::ARCH TTDevice::get_arch() const {
    return arch;
}

template<typename T>
T* TTDevice::get_register_address(std::uint32_t register_offset) {
    void *reg_mapping;
    if (system_reg_mapping != nullptr && register_offset >= system_reg_start_offset) {
        register_offset -= system_reg_offset_adjust;
        reg_mapping = system_reg_mapping;
    } else if (bar0_wc != bar0_uc && register_offset < bar0_wc_size) {
        reg_mapping = bar0_wc;
    } else {
        register_offset -= bar0_uc_offset;
        reg_mapping = bar0_uc;
    }
    return reinterpret_cast<T*>(static_cast<uint8_t*>(reg_mapping) + register_offset);
}

void TTDevice::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr) {
    void *dest = nullptr;
    if (bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        dest = reinterpret_cast<uint8_t *>(bar4_wc) + byte_addr;
    }else {
        dest = get_register_address<uint8_t>(byte_addr);
    }

    const void *src = reinterpret_cast<const void *>(buffer_addr);
    memcpy(dest, src, num_bytes);
// #ifndef DISABLE_ISSUE_3487_FIX
//     // memcpy_to_device(dest, src, num_bytes);
// #else
//     // ~4x faster than pci_read above, but works for all sizes and alignments
//     memcpy(dest, src, num_bytes);
// #endif
}

void TTDevice::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr) {
    void *src = nullptr;
    if (bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) { //arch == tt::ARCH::BLACKHOLE && 
        byte_addr -= BAR0_BH_SIZE;
        src = reinterpret_cast<uint8_t *>(bar4_wc) + byte_addr;
    } else {
        src = get_register_address<uint8_t>(byte_addr);
    }

    void *dest = reinterpret_cast<void *>(buffer_addr);
    memcpy(dest, src, num_bytes);
// #ifndef DISABLE_ISSUE_3487_FIX
//     // memcpy_from_device(dest, src, num_bytes);
// #else
//     // ~4x faster than pci_read above, but works for all sizes and alignments
//     memcpy(dest, src, num_bytes);
// #endif
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc
void TTDevice::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void TTDevice::write_regs(uint32_t byte_addr, uint32_t word_len, const void *data) {
    volatile uint32_t *dest = get_register_address<uint32_t>(byte_addr);
    const uint32_t *src = reinterpret_cast<const uint32_t*>(data);

    write_regs(dest, src, word_len);
}

void TTDevice::read_regs(uint32_t byte_addr, uint32_t word_len, void *data) {
    const volatile uint32_t *src = get_register_address<uint32_t>(byte_addr);
    uint32_t *dest = reinterpret_cast<uint32_t*>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
}

void TTDevice::write_tlb_reg(uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size){
    log_assert((tlb_cfg_reg_size == 8) or (tlb_cfg_reg_size == 12), "Tenstorrent hardware supports only 64bit or 96bit TLB config regs");

    volatile uint64_t *dest_qw = get_register_address<std::uint64_t>(byte_addr);
    volatile uint32_t *dest_extra_dw = get_register_address<std::uint32_t>(byte_addr+8);
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

//     LOG2(" TLB ");
//     print_buffer (&value_lower, sizeof(value_lower), true);
//     if (tlb_cfg_reg_size > 8) {
//         uint32_t* p_value_upper = reinterpret_cast<uint32_t*>(&value_upper);
//         print_buffer (p_value_upper, sizeof(uint32_t), true);
//     }
}
