/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/architecture_implementation.h"
#include "umd/device/pci_device.hpp"
#include "umd/device/tt_device/tlb_manager.h"

// TODO: Should be moved to blackhole_architecture_implementation.h
// See /vendor_ip/synopsys/052021/bh_pcie_ctl_gen5/export/configuration/DWC_pcie_ctl.h
static const uint64_t UNROLL_ATU_OFFSET_BAR = 0x1200;

// TODO: should be removed from tt_device.h, and put into blackhole_tt_device.h
// TODO: this is a bit of a hack... something to revisit when we formalize an
// abstraction for IO.
// BAR0 size for Blackhole, used to determine whether write block should use BAR0 or BAR4
static const uint64_t BAR0_BH_SIZE = 512 * 1024 * 1024;

constexpr unsigned int c_hang_read_value = 0xffffffffu;

struct dynamic_tlb {
    uint64_t bar_offset;      // Offset that address is mapped to, within the PCI BAR.
    uint64_t remaining_size;  // Bytes remaining between bar_offset and end of the TLB.
};

namespace boost::interprocess {
class named_mutex;
}

namespace tt::umd {

class TLBManager;

class TTDevice {
public:
    /**
     * Creates a proper TTDevice object for the given PCI device number.
     */
    static std::unique_ptr<TTDevice> create(int pci_device_number);
    TTDevice(std::unique_ptr<PCIDevice> pci_device, std::unique_ptr<architecture_implementation> architecture_impl);
    virtual ~TTDevice() = default;

    architecture_implementation *get_architecture_implementation();
    PCIDevice *get_pci_device();
    TLBManager *get_tlb_manager();

    tt::ARCH get_arch();

    void detect_hang_read(uint32_t data_read = c_hang_read_value);

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
    void write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len);
    void write_regs(uint32_t byte_addr, uint32_t word_len, const void *data);
    void read_regs(uint32_t byte_addr, uint32_t word_len, void *data);
    void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);
    void write_to_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

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
        std::uint64_t ordering);
    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index,
        tt_xy_pair target,
        std::uint64_t address,
        std::uint64_t ordering = tt::umd::tlb_data::Relaxed);
    dynamic_tlb set_dynamic_tlb_broadcast(
        unsigned int tlb_index,
        std::uint64_t address,
        tt_xy_pair start,
        tt_xy_pair end,
        std::uint64_t ordering = tt::umd::tlb_data::Relaxed);

    /**
     * Configures a PCIe Address Translation Unit (iATU) region.
     *
     * Device software expects to be able to access memory that is shared with
     * the host using the following NOC addresses at the PCIe core:
     * - GS: 0x0
     * - WH: 0x8_0000_0000
     * - BH: 0x1000_0000_0000_0000
     * Without iATU configuration, these map to host PA 0x0.
     *
     * While modern hardware supports IOMMU with flexible IOVA mapping, we must
     * maintain the iATU configuration to satisfy software that has hard-coded
     * the above NOC addresses rather than using driver-provided IOVAs.
     *
     * This interface is only intended to be used for configuring sysmem with
     * either 1GB hugepages or a compatible scheme.
     *
     * @param region iATU region index (0-15)
     * @param base region * (1 << 30)
     * @param target DMA address (PA or IOVA) to map to
     * @param size size of the mapping window; must be (1 << 30)
     *
     * NOTE: Programming the iATU from userspace is architecturally incorrect:
     * - iATU should be managed by KMD to ensure proper cleanup on process exit
     * - Multiple processes can corrupt each other's iATU configurations
     * We should fix this!
     */
    virtual void configure_iatu_region(size_t region, uint64_t base, uint64_t target, size_t size);

protected:
    std::unique_ptr<PCIDevice> pci_device_;
    std::unique_ptr<architecture_implementation> architecture_impl_;
    std::unique_ptr<TLBManager> tlb_manager_;
    tt::ARCH arch;

    bool is_hardware_hung();

    template <typename T>
    T *get_register_address(uint32_t register_offset);

    // Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
    // Both routines assume that misaligned accesses are permitted on host memory.
    //
    // 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
    // which glibc's memcpy may perform when unrolling. This affects from and to device.
    // 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
    // to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
    void memcpy_to_device(void *dest, const void *src, std::size_t num_bytes);
    void memcpy_from_device(void *dest, const void *src, std::size_t num_bytes);

    void create_read_write_mutex();

    std::shared_ptr<boost::interprocess::named_mutex> read_write_mutex = nullptr;
};
}  // namespace tt::umd
