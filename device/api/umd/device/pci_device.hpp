/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>

#include "fmt/format.h"
#include "umd/device/semver.hpp"
#include "umd/device/tlb.h"
#include "umd/device/tt_arch_types.h"
#include "umd/device/tt_cluster_descriptor_types.h"
#include "umd/device/tt_xy_pair.h"

// TODO: this is used up in cluster.cpp but that logic ought to be
// lowered into the PCIDevice class since it is specific to PCIe cards.
// See /vendor_ip/synopsys/052021/bh_pcie_ctl_gen5/export/configuration/DWC_pcie_ctl.h
static const uint64_t UNROLL_ATU_OFFSET_BAR = 0x1200;

// TODO: this is a bit of a hack... something to revisit when we formalize an
// abstraction for IO.
// BAR0 size for Blackhole, used to determine whether write block should use BAR0 or BAR4
static const uint64_t BAR0_BH_SIZE = 512 * 1024 * 1024;

constexpr unsigned int c_hang_read_value = 0xffffffffu;

namespace tt::umd {
class TTDevice;
struct semver_t;
}  // namespace tt::umd

struct dynamic_tlb {
    uint64_t bar_offset;      // Offset that address is mapped to, within the PCI BAR.
    uint64_t remaining_size;  // Bytes remaining between bar_offset and end of the TLB.
};

struct hugepage_mapping {
    void *mapping = nullptr;
    size_t mapping_size = 0;
    uint64_t physical_address = 0;
};

struct PciDeviceInfo {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t pci_domain;
    uint16_t pci_bus;
    uint16_t pci_device;
    uint16_t pci_function;

    tt::ARCH get_arch() const;
    // TODO: does it make sense to move attributes that we can read from sysfs
    // onto this struct as methods?  e.g. current_link_width etc.
};

// Do we want to put everything into this file into tt::umd namespace?
using tt::umd::semver_t;

class PCIDevice {
    const std::string device_path;   // Path to character device: /dev/tenstorrent/N
    const int pci_device_num;        // N in /dev/tenstorrent/N
    const int logical_id;            // Unique identifier for each device in entire network topology
    const int pci_device_file_desc;  // Character device file descriptor
    const PciDeviceInfo info;        // PCI device info
    const int numa_node;             // -1 if non-NUMA
    const int revision;              // PCI revision value from sysfs
    const tt::ARCH arch;             // e.g. Grayskull, Wormhole, Blackhole
    const semver_t kmd_version;      // KMD version
    std::unique_ptr<tt::umd::TTDevice> tt_device;

public:
    /**
     * @return a list of integers corresponding to character devices in /dev/tenstorrent/
     */
    static std::vector<int> enumerate_devices();

    /**
     * @return a map of PCI device numbers (/dev/tenstorrent/N) to PciDeviceInfo
     */
    static std::map<int, PciDeviceInfo> enumerate_devices_info();

    /**
     * PCI device constructor.
     *
     * Opens the character device file descriptor, reads device information from
     * sysfs, and maps device memory region(s) into the process address space.
     *
     * @param pci_device_number     N in /dev/tenstorrent/N
     * @param logical_device_id     unique identifier for this device in the network topology
     */
    PCIDevice(int pci_device_number, int logical_device_id = 0);

    /**
     * PCIDevice destructor.
     * Unmaps device memory and closes chardev file descriptor.
     */
    ~PCIDevice();

    PCIDevice(const PCIDevice &) = delete;       // copy
    void operator=(const PCIDevice &) = delete;  // copy assignment

    /**
     * @return PCI device info
     */
    const PciDeviceInfo get_device_info() const { return info; }

    /**
     * @return which NUMA node this device is associated with, or -1 if non-NUMA
     */
    int get_numa_node() const { return numa_node; }

    /**
     * @return underlying file descriptor
     * TODO: this is an abstraction violation to be removed when this class
     * assumes control over hugepage/DMA mapping code.
     */
    int get_fd() const { return pci_device_file_desc; }

    /**
     * @return N in /dev/tenstorrent/N
     * TODO: target for removal; upper layers should not care about this.
     */
    int get_device_num() const { return pci_device_num; }

    /**
     * @return unique integer for each device in entire network topology
     * TODO: target for removal; upper layers shouldn't to pass this in here. It
     * is unused by this class.
     */
    int get_logical_id() const { return logical_id; }

    /**
     * @return PCI device id
     */
    int get_pci_device_id() const { return info.device_id; }

    /**
     * @return PCI revision value from sysfs.
     * TODO: target for removal; upper layers should not care about this.
     */
    int get_pci_revision() const { return revision; }

    /**
     * @return what architecture this device is (e.g. Wormhole, Blackhole, etc.)
     */
    tt::ARCH get_arch() const { return arch; }

    // Note: byte_addr is (mostly but not always) offset into BAR0.  This
    // interface assumes the caller knows what they are doing - but it's unclear
    // how to use this interface correctly without knowing details of the chip
    // and its state.
    // TODO: build a proper abstraction for IO.  At this level, that is access
    // to registers in BAR0 (although possibly the right abstraction is to add
    // methods that perform specific operations as opposed to generic register
    // read/write methods) and access to segments of BAR0/4 that are mapped to
    // NOC endpoints.  Probably worth waiting for the KMD to start owning the
    // resource management aspect of these PCIe->NOC mappings (the "TLBs")
    // before doing too much work here...
    void write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t *buffer_addr);
    void read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t *buffer_addr);
    void write_regs(uint32_t byte_addr, uint32_t word_len, const void *data);
    void write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len);
    void read_regs(uint32_t byte_addr, uint32_t word_len, void *data);

    // TLB related functions.
    // TODO: These are architecture specific, and will be moved out of the class.
    void write_tlb_reg(
        uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size);
    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index,
        tt_xy_pair start,
        tt_xy_pair end,
        std::uint64_t address,
        bool multicast,
        std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>> &harvested_coord_translation,
        std::uint64_t ordering);
    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index,
        tt_xy_pair target,
        std::uint64_t address,
        std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>> &harvested_coord_translation,
        std::uint64_t ordering = tt::umd::tlb_data::Relaxed);
    dynamic_tlb set_dynamic_tlb_broadcast(
        unsigned int tlb_index,
        std::uint64_t address,
        std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>> &harvested_coord_translation,
        tt_xy_pair start,
        tt_xy_pair end,
        std::uint64_t ordering = tt::umd::tlb_data::Relaxed);

    tt::umd::TTDevice *get_tt_device() const;
    void detect_hang_read(uint32_t data_read = c_hang_read_value);

    // TODO: this also probably has more sense to live in the future TTDevice class.
    bool init_hugepage(uint32_t num_host_mem_channels);
    int get_num_host_mem_channels() const;
    hugepage_mapping get_hugepage_mapping(int channel) const;

public:
    // TODO: we can and should make all of these private.
    void *bar0_uc = nullptr;
    size_t bar0_uc_size = 0;
    size_t bar0_uc_offset = 0;

    void *bar0_wc = nullptr;
    size_t bar0_wc_size = 0;

    void *bar2_uc = nullptr;
    size_t bar2_uc_size;

    void *bar4_wc = nullptr;
    uint64_t bar4_wc_size;

    // TODO: let's get rid of this unless we need to run UMD on WH systems with
    // shrunk BAR0.  If we don't (and we shouldn't), then we can just use BAR0
    // and simplify the code.
    void *system_reg_mapping = nullptr;
    size_t system_reg_mapping_size;
    uint32_t system_reg_start_offset;   // Registers >= this are system regs, use the mapping.
    uint32_t system_reg_offset_adjust;  // This is the offset of the first reg in the system reg mapping.

    uint32_t read_checking_offset;

private:
    bool is_hardware_hung();

    template <typename T>
    T *get_register_address(uint32_t register_offset);

    // For debug purposes when various stages fails.
    void print_file_contents(std::string filename, std::string hint = "");

    semver_t read_kmd_version();

    std::vector<hugepage_mapping> hugepage_mapping_per_channel;
};
