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
#include <sys/stat.h> // for fstat
#include <linux/pci.h> // for PCI_SLOT, PCI_FUNC

#include "umd/device/pci_device.hpp"
#include "ioctl.h"

#include "ioctl.h"
#include "umd/device/tt_arch_types.h"
#include "umd/device/driver_atomics.h"
#include "umd/device/architecture_implementation.h"
#include "cpuset_lib.hpp"
#include "umd/device/hugepage.h"
#include "assert.hpp"
#include "logger.hpp"

static const uint16_t GS_PCIE_DEVICE_ID = 0xfaca;
static const uint16_t WH_PCIE_DEVICE_ID = 0x401e;
static const uint16_t BH_PCIE_DEVICE_ID = 0xb140;

// TODO: we'll have to rethink this when KMD takes control of the inbound PCIe
// TLB windows and there is no longer a pre-defined WC/UC split.
static const uint32_t GS_BAR0_WC_MAPPING_SIZE = (156<<20) + (10<<21) + (18<<24);

// Defines the address for WC region. addresses 0 to BH_BAR0_WC_MAPPING_SIZE are in WC, above that are UC
static const uint32_t BH_BAR0_WC_MAPPING_SIZE = 188<<21;

static const uint32_t BH_NOC_NODE_ID_OFFSET = 0x1FD04044;
static const uint32_t GS_WH_ARC_SCRATCH_6_OFFSET = 0x1FF30078;

// Hugepages must be 1GB in size
const uint32_t HUGEPAGE_REGION_SIZE = 1 << 30; // 1GB

using namespace tt;
using namespace tt::umd;

template <typename T>
static T read_sysfs(const PciDeviceInfo &device_info, const std::string &attribute_name) {
    const auto sysfs_path = fmt::format("/sys/bus/pci/devices/{:04x}:{:02x}:{:02x}.{:x}/{}",
                                        device_info.pci_domain, device_info.pci_bus,
                                        device_info.pci_device, device_info.pci_function, attribute_name);
    std::ifstream attribute_file(sysfs_path);
    std::string value_str;
    T value;

    if (!std::getline(attribute_file, value_str)) {
        TT_THROW("Failed reading sysfs attribute: {}", sysfs_path);
    }

    std::istringstream iss(value_str);

    // Handle hexadecimal input for integer types
    if constexpr (std::is_integral_v<T>) {
        if (value_str.substr(0, 2) == "0x") {
            iss >> std::hex;
        }
    }

    if (!(iss >> value)) {
        TT_THROW("Failed to parse sysfs attribute value: {}", value_str);
    }

    return value;
}

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

static tt::ARCH detect_arch(uint32_t pcie_device_id, uint32_t pcie_revision_id) {
    if (pcie_device_id == GS_PCIE_DEVICE_ID){
        return tt::ARCH::GRAYSKULL;
    } else if (pcie_device_id == WH_PCIE_DEVICE_ID && pcie_revision_id == 0x01){
        return tt::ARCH::WORMHOLE_B0;
    } else if (pcie_device_id == BH_PCIE_DEVICE_ID){
        return tt::ARCH::BLACKHOLE;
    } else {
        TT_THROW("Unknown pcie device id that does not match any known architecture: ", pcie_device_id);
    }
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

tt::ARCH PciDeviceInfo::get_arch() const {
    if (this->device_id == GS_PCIE_DEVICE_ID){
        return tt::ARCH::GRAYSKULL;
    } else if (this->device_id == WH_PCIE_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (this->device_id == BH_PCIE_DEVICE_ID){
        return tt::ARCH::BLACKHOLE;
    }
    return tt::ARCH::Invalid;
}


/* static */ std::vector<int> PCIDevice::enumerate_devices() {
    std::vector<int> device_ids;
    std::string path = "/dev/tenstorrent/";

    if (!std::filesystem::exists(path)) {
        return device_ids;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        std::string filename = entry.path().filename().string();

        // TODO: this will skip any device that has a non-numeric name, which
        // is probably what we want longer-term (i.e. a UUID or something).
        if (std::all_of(filename.begin(), filename.end(), ::isdigit)) {
            device_ids.push_back(std::stoi(filename));
        }
    }

    std::sort(device_ids.begin(), device_ids.end());
    return device_ids;
}

/* static */ std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    std::map<int, PciDeviceInfo> infos;
    for (int n : PCIDevice::enumerate_devices()) {
        int fd = open(fmt::format("/dev/tenstorrent/{}", n).c_str(), O_RDWR | O_CLOEXEC);
        if (fd == -1) {
            continue;
        }

        try {
            infos[n] = read_device_info(fd);
        } catch (...) {}

        close(fd);
    }
    return infos;
}

PCIDevice::PCIDevice(int pci_device_number, int logical_device_id)
    : device_path(fmt::format("/dev/tenstorrent/{}", pci_device_number))
    , pci_device_num(pci_device_number)
    , logical_id(logical_device_id)
    , pci_device_file_desc(open(device_path.c_str(), O_RDWR | O_CLOEXEC))
    , info(read_device_info(pci_device_file_desc))
    , numa_node(read_sysfs<int>(info, "numa_node"))
    , revision(read_sysfs<int>(info, "revision"))
    , arch(detect_arch(info.device_id, revision))
    , architecture_implementation(tt::umd::architecture_implementation::create(arch))
{
    struct {
        tenstorrent_query_mappings query_mappings;
        tenstorrent_mapping mapping_array[8];
    } mappings;
    memset(&mappings, 0, sizeof(mappings));
    mappings.query_mappings.in.output_mapping_count = 8;

    if (ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        throw std::runtime_error(fmt::format("Query mappings failed on device {}.", pci_device_num));
    }

    // Mapping resource to BAR
    // Resource 0 -> BAR0
    // Resource 1 -> BAR2
    // Resource 2 -> BAR4
    tenstorrent_mapping bar0_uc_mapping{};
    tenstorrent_mapping bar0_wc_mapping{};
    tenstorrent_mapping bar2_uc_mapping{};
    tenstorrent_mapping bar2_wc_mapping{};
    tenstorrent_mapping bar4_uc_mapping{};
    tenstorrent_mapping bar4_wc_mapping{};

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
        throw std::runtime_error(fmt::format("Device {} has no BAR0 UC mapping.", pci_device_num));
    }

    auto wc_mapping_size = arch == tt::ARCH::BLACKHOLE ? BH_BAR0_WC_MAPPING_SIZE : GS_BAR0_WC_MAPPING_SIZE;

    // Attempt WC mapping first so we can fall back to all-UC if it fails.
    if (bar0_wc_mapping.mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
        bar0_wc_size = std::min<size_t>(bar0_wc_mapping.mapping_size, wc_mapping_size);
        bar0_wc = mmap(NULL, bar0_wc_size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, bar0_wc_mapping.mapping_base);
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

    bar0_uc = mmap(NULL, bar0_uc_size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, bar0_uc_mapping.mapping_base + bar0_uc_offset);

    if (bar0_uc == MAP_FAILED) {
        throw std::runtime_error(fmt::format("BAR0 UC mapping failed for device {}.", pci_device_num));
    }

    if (!bar0_wc) {
        bar0_wc = bar0_uc;
    }

    if (arch == tt::ARCH::WORMHOLE_B0) {
        if (bar4_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 UC mapping.", pci_device_num));
        }

        system_reg_mapping_size = bar4_uc_mapping.mapping_size;

        system_reg_mapping = mmap(NULL, bar4_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, bar4_uc_mapping.mapping_base);

        if (system_reg_mapping == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR4 UC mapping failed for device {}.", pci_device_num));
        }

        system_reg_start_offset = (512 - 16) * 1024*1024;
        system_reg_offset_adjust = (512 - 32) * 1024*1024;
    } else if(arch == tt::ARCH::BLACKHOLE) {
        if (bar2_uc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE1_UC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR2 UC mapping.", pci_device_num));
        }

        // Using UnCachable memory mode. This is used for accessing registers on Blackhole.
        bar2_uc_size = bar2_uc_mapping.mapping_size;
        bar2_uc = mmap(NULL, bar2_uc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, bar2_uc_mapping.mapping_base);

        if (bar2_uc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR2 UC mapping failed for device {}.", pci_device_num));
        }

        if (bar4_wc_mapping.mapping_id != TENSTORRENT_MAPPING_RESOURCE2_WC) {
            throw std::runtime_error(fmt::format("Device {} has no BAR4 WC mapping.", pci_device_num));
        }

        // Using Write-Combine memory mode. This is used for accessing DRAM on Blackhole.
        // WC doesn't guarantee write ordering but has better performance.
        bar4_wc_size = bar4_wc_mapping.mapping_size;
        bar4_wc = mmap(NULL, bar4_wc_mapping.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device_file_desc, bar4_wc_mapping.mapping_base);

        if (bar4_wc == MAP_FAILED) {
            throw std::runtime_error(fmt::format("BAR4 WC mapping failed for device {}.", pci_device_num));
        }
    }

    // GS+WH: ARC_SCRATCH[6], BH: NOC NODE_ID
    read_checking_offset = arch == tt::ARCH::BLACKHOLE ? BH_NOC_NODE_ID_OFFSET : GS_WH_ARC_SCRATCH_6_OFFSET;
}

PCIDevice::~PCIDevice() {
    for (const auto& hugepage_mapping : hugepage_mapping_per_channel) {
        if (hugepage_mapping.mapping) {
            munmap(hugepage_mapping.mapping, hugepage_mapping.mapping_size);
        }
    }

    if (arch == tt::ARCH::BLACKHOLE && bar2_uc != nullptr && bar2_uc != MAP_FAILED) {
        // Disable ATU index 0
        // TODO: Implement disabling for all indexes, once more host channels are enabled.

        // This is not going to happen if the application crashes, so if it's
        // essential for correctness then it needs to move to the driver.
        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;
        uint32_t region_ctrl_2 = 0 << 31; // REGION_EN = 0
        write_regs(reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(bar2_uc) + iatu_base + 0x04), &region_ctrl_2, 1);
    }

    close(pci_device_file_desc);

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
}

template<typename T>
T* PCIDevice::get_register_address(uint32_t register_offset) {
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

    if (num_bytes >= sizeof(std::uint32_t)) {
        detect_hang_read(*reinterpret_cast<std::uint32_t*>(dest));
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

void PCIDevice::write_tlb_reg(uint32_t byte_addr, uint64_t value_lower, uint64_t value_upper, uint32_t tlb_cfg_reg_size){
    log_assert((tlb_cfg_reg_size == 8) or (tlb_cfg_reg_size == 12), "Tenstorrent hardware supports only 64bit or 96bit TLB config regs");

    volatile uint64_t *dest_qw = get_register_address<uint64_t>(byte_addr);
    volatile uint32_t *dest_extra_dw = get_register_address<uint32_t>(byte_addr+8);
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

bool PCIDevice::is_hardware_hung() {
    volatile const void *addr = reinterpret_cast<const char *>(bar0_uc) + (get_architecture_implementation()->get_arc_reset_scratch_offset() + 6 * 4) - bar0_uc_offset;
    std::uint32_t scratch_data = *reinterpret_cast<const volatile std::uint32_t*>(addr);

    return (scratch_data == c_hang_read_value);
}

void PCIDevice::detect_hang_read(std::uint32_t data_read) {
    if (data_read == c_hang_read_value && is_hardware_hung()) {
        std::uint32_t scratch_data = *get_register_address<std::uint32_t>(read_checking_offset);

        throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
    }
}

// Get TLB index (from zero), check if it's in 16MB, 2MB or 1MB TLB range, and dynamically program it.
dynamic_tlb PCIDevice::set_dynamic_tlb(unsigned int tlb_index, tt_xy_pair start, tt_xy_pair end,
                            std::uint64_t address, bool multicast, std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>>& harvested_coord_translation, std::uint64_t ordering) {
    auto architecture_implementation = get_architecture_implementation();
    if (multicast) {
        std::tie(start, end) = architecture_implementation->multicast_workaround(start, end);
    }

    log_trace(LogSiliconDriver, "set_dynamic_tlb with arguments: tlb_index = {}, start = ({}, {}), end = ({}, {}), address = 0x{:x}, multicast = {}, ordering = {}",
         tlb_index, start.x, start.y, end.x, end.y, address, multicast, (int)ordering);

    tt::umd::tlb_configuration tlb_config = architecture_implementation->get_tlb_configuration(tlb_index);
    std::uint32_t TLB_CFG_REG_SIZE_BYTES = architecture_implementation->get_tlb_cfg_reg_size_bytes();
    auto translated_start_coords = harvested_coord_translation.at(logical_id).at(start);
    auto translated_end_coords = harvested_coord_translation.at(logical_id).at(end);
    uint32_t tlb_address    = address / tlb_config.size;
    uint32_t local_address   = address % tlb_config.size;
    uint64_t tlb_base       = tlb_config.base + (tlb_config.size * tlb_config.index_offset);
    uint32_t tlb_cfg_reg    = tlb_config.cfg_addr + (TLB_CFG_REG_SIZE_BYTES * tlb_config.index_offset);

    std::pair<std::uint64_t, std::uint64_t> tlb_data = tt::umd::tlb_data {
        .local_offset = tlb_address,
        .x_end = static_cast<uint64_t>(translated_end_coords.x),
        .y_end = static_cast<uint64_t>(translated_end_coords.y),
        .x_start = static_cast<uint64_t>(translated_start_coords.x),
        .y_start = static_cast<uint64_t>(translated_start_coords.y),
        .mcast = multicast,
        .ordering = ordering,
        // TODO #2715: hack for Blackhole A0, will potentially be fixed in B0.
        // Using the same static vc for reads and writes through TLBs can hang the card. It doesn't even have to be the same TLB.
        // Dynamic vc should not have this issue. There might be a perf impact with using dynamic vc.
        .static_vc = (get_arch() == tt::ARCH::BLACKHOLE) ? false : true,
    }.apply_offset(tlb_config.offset);

    log_debug(LogSiliconDriver, "set_dynamic_tlb() with tlb_index: {} tlb_index_offset: {} dynamic_tlb_size: {}MB tlb_base: 0x{:x} tlb_cfg_reg: 0x{:x}", tlb_index, tlb_config.index_offset, tlb_config.size/(1024*1024), tlb_base, tlb_cfg_reg);
    write_tlb_reg(tlb_cfg_reg, tlb_data.first, tlb_data.second, TLB_CFG_REG_SIZE_BYTES);

    return { tlb_base + local_address, tlb_config.size - local_address };
}

dynamic_tlb PCIDevice::set_dynamic_tlb(unsigned int tlb_index, tt_xy_pair target, std::uint64_t address, std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>>& harvested_coord_translation, std::uint64_t ordering) {
    return set_dynamic_tlb(tlb_index, tt_xy_pair(0, 0), target, address, false, harvested_coord_translation, ordering);
}

dynamic_tlb PCIDevice::set_dynamic_tlb_broadcast(unsigned int tlb_index, std::uint64_t address, std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>>& harvested_coord_translation, tt_xy_pair start, tt_xy_pair end, std::uint64_t ordering) {
    // Issue a broadcast to cores included in the start (top left) and end (bottom right) grid
    return set_dynamic_tlb(tlb_index, start, end, address, true, harvested_coord_translation, ordering);
}

tt::umd::architecture_implementation* PCIDevice::get_architecture_implementation() const {return architecture_implementation.get();}

bool PCIDevice::init_hugepage(uint32_t num_host_mem_channels) {
    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;

    // Convert from logical (device_id in netlist) to physical device_id (in case of virtualization)
    auto physical_device_id = get_device_num();

    std::string hugepage_dir = find_hugepage_dir(hugepage_size);
    if (hugepage_dir.empty()) {
        log_warning(LogSiliconDriver, "ttSiliconDevice::init_hugepage: no huge page mount found for hugepage_size: {}.", hugepage_size);
        return false;
    }

    bool success = true;

    hugepage_mapping_per_channel.resize(num_host_mem_channels);

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < num_host_mem_channels; ch++) {

        int hugepage_fd = open_hugepage_file(hugepage_dir, physical_device_id, ch);
        if (hugepage_fd == -1) {
            // Probably a permissions problem.
            log_warning(LogSiliconDriver, "ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} creating hugepage mapping file failed.", physical_device_id, ch);
            success = false;
            continue;
        }

        // Verify opened file size.
        struct stat hugepage_st;
        if (fstat(hugepage_fd, &hugepage_st) == -1) {
            log_warning(LogSiliconDriver, "Error reading hugepage file size after opening.");
        }

        std::byte *mapping = static_cast<std::byte*>(mmap(nullptr, hugepage_size, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_POPULATE, hugepage_fd, 0));

        close(hugepage_fd);

        if (mapping == MAP_FAILED) {
            log_warning(LogSiliconDriver, "UMD: Mapping a hugepage failed. (device: {}, {}/{} errno: {}).", physical_device_id, ch, num_host_mem_channels, strerror(errno));
            if (hugepage_st.st_size == 0) {
                log_warning(LogSiliconDriver, "Opened hugepage file has zero size, mapping might've failed due to that. Verify that enough hugepages are provided.");
            }
            print_file_contents("/proc/cmdline");\
            print_file_contents("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages"); // Hardcoded for 1GB hugepage.
            success = false;
            continue;
        }

        // Beter performance if hugepage just allocated (populate flag to prevent lazy alloc) is migrated to same numanode as TT device.
        if (!tt::cpuset::tt_cpuset_allocator::bind_area_to_memory_nodeset(physical_device_id, mapping, hugepage_size)){
            log_warning(LogSiliconDriver, "---- ttSiliconDevice::init_hugepage: bind_area_to_memory_nodeset() failed (physical_device_id: {} ch: {}). "
            "Hugepage allocation is not on NumaNode matching TT Device. Side-Effect is decreased Device->Host perf (Issue #893).",
            physical_device_id, ch);
        }

        tenstorrent_pin_pages pin_pages;
        memset(&pin_pages, 0, sizeof(pin_pages));
        pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
        pin_pages.in.flags = TENSTORRENT_PIN_PAGES_CONTIGUOUS;
        pin_pages.in.virtual_address = reinterpret_cast<std::uintptr_t>(mapping);
        pin_pages.in.size = hugepage_size;

        auto fd = get_fd();

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) == -1) {
            log_warning(LogSiliconDriver, "---- ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} TENSTORRENT_IOCTL_PIN_PAGES failed (errno: {}). Common Issue: Requires TTMKD >= 1.11, see following file contents...", physical_device_id, ch, strerror(errno));
            munmap(mapping, hugepage_size);
            print_file_contents("/sys/module/tenstorrent/version", "(TTKMD version)");
            print_file_contents("/proc/meminfo");
            print_file_contents("/proc/buddyinfo");
            success = false;
            continue;
        }

        hugepage_mapping_per_channel[ch] = {mapping, hugepage_size, pin_pages.out.physical_address};

        log_debug(LogSiliconDriver, "ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} mapping_size: {} physical address 0x{:x}", physical_device_id, ch, hugepage_size, (unsigned long long)hugepage_mappings.at(device_id).at(ch).physical_address);
    }

    return success;
}

int PCIDevice::get_num_host_mem_channels() const {
    return hugepage_mapping_per_channel.size();
}

hugepage_mapping PCIDevice::get_hugepage_mapping(int channel) const {
    if (channel < 0 || hugepage_mapping_per_channel.size() <= channel) {
        return {nullptr, 0, 0};
    } else {
        return hugepage_mapping_per_channel[channel];
    }
}

void PCIDevice::print_file_contents(std::string filename, std::string hint){
    if (std::filesystem::exists(filename)){
        std::ifstream meminfo(filename);
        if (meminfo.is_open()){
            std::cout << std::endl << "File " << filename << " " << hint << " is: " << std::endl;
            std::cout << meminfo.rdbuf();
        }
    }
}
