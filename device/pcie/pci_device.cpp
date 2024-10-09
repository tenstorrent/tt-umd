/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <cstring> // for memcpy
#include <vector>
#include <fcntl.h>  // for ::open
#include <unistd.h> // for ::close
#include <sys/ioctl.h> // for ioctl
#include <sys/mman.h>  // for mmap, munmap
#include <linux/pci.h> // for PCI_SLOT, PCI_FUNC

#include "pci_device.hpp"
#include "utils.hpp"

#include "ioctl.h"
#include "device/tt_arch_types.h"
#include "device/driver_atomics.h"
#include "device/architecture_implementation.h"
#include "common/assert.hpp"
#include "common/logger.hpp"

static PciDeviceInfo read_device_info(int fd)
{
    tenstorrent_get_device_info info{};
    info.in.output_size_bytes = sizeof(info.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) < 0) {
        TT_THROW("TENSTORRENT_IOCTL_GET_DEVICE_INFO failed");
    }

    uint16_t bus = info.out.bus_dev_fn >> 8;
    uint16_t dev = (info.out.bus_dev_fn >> 3) & 0x1F;
    uint16_t fn = info.out.bus_dev_fn & 0x07;

    return PciDeviceInfo{info.out.vendor_id, info.out.device_id, info.out.pci_domain, bus, dev, fn};
}

static int determine_numa_node(int fd)
{
    const auto device_info = read_device_info(fd);
    const auto sysfs_path = fmt::format("/sys/bus/pci/devices/{:04x}:{:02x}:{:02x}.{}/numa_node",
                                        device_info.pci_domain, device_info.pci_bus,
                                        device_info.pci_device, device_info.pci_function);

    std::ifstream numa_file(sysfs_path);
    int numa_node = -1;
    if (numa_file >> numa_node) {
        return numa_node;
    }
    return -1;
}


tt::ARCH detect_arch(int device_id){
    std::uint32_t pcie_device_id = get_pcie_info(device_id, "pcie_device_id");
    std::uint32_t pcie_revision_id = get_pcie_info(device_id, "revision");
    return detect_arch(pcie_device_id, pcie_revision_id);
}

// Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
// Both routines assume that misaligned accesses are permitted on host memory.
//
// 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
// which glibc's memcpy may perform when unrolling. This affects from and to device.
// 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
// to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
inline void memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
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

inline void memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
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

// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------

PCIDevice::PCIDevice(int device_id, int logical_device_id) {
    // TODO: use C++ constructor to do everything
    // TODO: make public member vars const
    // TODO: get logical_id out of here
    this->device_id = device_id;
    this->logical_id = logical_device_id;
    setup_device();

    this->info = read_device_info(device_fd);

}

PCIDevice::~PCIDevice() {
    close_device();
}


void PCIDevice::setup_device() {
    this->device_fd = find_device(this->device_id);
    this->numa_node = determine_numa_node(this->device_fd);
    this->pcie_device_id = get_pcie_info(this->device_id, "pcie_device_id");
    this->pcie_revision_id = get_pcie_info(this->device_id, "revision");
    this->arch = detect_arch(pcie_device_id, pcie_revision_id);
    this->architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch));

    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;
    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(device_fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(fmt::format("Query mappings failed on device {}.", device_id));
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
        throw std::runtime_error(fmt::format("Device {} has no BAR0 UC mapping.", device_id));
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
        throw std::runtime_error(fmt::format("BAR0 UC mapping failed for device {}.", device_id));
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (arch == tt::ARCH::WORMHOLE_B0) {
        if (bar4_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 UC mapping.", device_id));
        }

        system_reg_mapping_size = bar4_uc_mapping.mapping_size;

        system_reg_mapping = mmap(NULL, bar4_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar4_uc_mapping.mapping_base);

        if (system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR4 UC mapping failed for device {}.", device_id));
        }

        system_reg_start_offset = (512 - 16) * 1024*1024;
        system_reg_offset_adjust = (512 - 32) * 1024*1024;
    } else if(arch == tt::ARCH::BLACKHOLE) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE1_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR2 UC mapping.", device_id));
        }

        // Using UnCachable memory mode. This is used for accessing registers on Blackhole.
        bar2_uc_size = bar2_uc_mapping.mapping_size;
        bar2_uc = mmap(NULL, bar2_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar2_uc_mapping.mapping_base);

        if (bar2_uc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR2 UC mapping failed for device {}.", device_id));
        }

        if (bar4_wc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_WC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 WC mapping.", device_id));
        }

        // Using Write-Combine memory mode. This is used for accessing DRAM on Blackhole.
        // WC doesn't guarantee write ordering but has better performance.
        bar4_wc_size = bar4_wc_mapping.mapping_size;
        bar4_wc = mmap(NULL, bar4_wc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, bar4_wc_mapping.mapping_base);

        if (bar4_wc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR4 WC mapping failed for device {}.", device_id));
        }
    }

    // GS+WH: ARC_SCRATCH[6], BH: NOC NODE_ID
    read_checking_offset = arch == tt::ARCH::BLACKHOLE ? BH_NOC_NODE_ID_OFFSET : GS_WH_ARC_SCRATCH_6_OFFSET;
}

void PCIDevice::close_device() {
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

// Open a unique device_id per host memory channel (workaround for ttkmd < 1.21 support for more than 1 pin per fd)
void PCIDevice::open_hugepage_per_host_mem_ch(uint32_t num_host_mem_channels) {
    for (int ch = 0; ch < num_host_mem_channels; ch++) {
        log_debug(LogSiliconDriver, "Opening device_fd_per_host_ch device index: {} ch: {} (num_host_mem_channels: {})", device_id, ch, num_host_mem_channels);
        int device_fd_for_host_mem = find_device(device_id);
        if (device_fd_for_host_mem == -1) {
            throw std::runtime_error(fmt::format("Failed opening a host memory device handle for device {}.", device_id));
        }
        device_fd_per_host_ch.push_back(device_fd_for_host_mem);
    }
}

tt::ARCH PCIDevice::get_arch() const {
    return arch;
}

template<typename T>
T* PCIDevice::get_register_address(std::uint32_t register_offset) {
    // Right now, address can either be exposed register in BAR, or TLB window in BAR0 (BAR4 for Blackhole).
    // Should clarify this interface
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

void PCIDevice::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr) {
    void *dest = nullptr;
    if (bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        dest = reinterpret_cast<uint8_t *>(bar4_wc) + byte_addr;
    } else {
        dest = get_register_address<uint8_t>(byte_addr);
    }

    const void *src = reinterpret_cast<const void *>(buffer_addr);
    if (arch == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device(dest, src, num_bytes);
    } else {
        memcpy(dest, src, num_bytes);
    }
}

void PCIDevice::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr) {
    void *src = nullptr;
    if (bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        src = reinterpret_cast<uint8_t *>(bar4_wc) + byte_addr;
    } else {
        src = get_register_address<uint8_t>(byte_addr);
    }

    void *dest = reinterpret_cast<void *>(buffer_addr);
    if (arch == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(dest, src, num_bytes);
    } else {
        memcpy(dest, src, num_bytes);
    }
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc
void PCIDevice::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void PCIDevice::write_regs(uint32_t byte_addr, uint32_t word_len, const void *data) {
    volatile uint32_t *dest = get_register_address<uint32_t>(byte_addr);
    const uint32_t *src = reinterpret_cast<const uint32_t*>(data);

    write_regs(dest, src, word_len);
}

void PCIDevice::read_regs(uint32_t byte_addr, uint32_t word_len, void *data) {
    const volatile uint32_t *src = get_register_address<uint32_t>(byte_addr);
    uint32_t *dest = reinterpret_cast<uint32_t*>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
}

void PCIDevice::write_tlb_reg(uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size){
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
}
