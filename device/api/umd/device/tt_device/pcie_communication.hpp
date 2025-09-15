/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_communication.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

struct dynamic_tlb {
    uint64_t bar_offset;      // Offset that address is mapped to, within the PCI BAR.
    uint64_t remaining_size;  // Bytes remaining between bar_offset and end of the TLB.
};

class PCIeCommunication : TTDeviceCommunication {
public:
    PCIeCommunication(
        LockManager& lock_manager, PCIDevice& pci_device, architecture_implementation& architecture_implementation) :
        lock_manager(lock_manager), pci_device(pci_device), architecture_implementation(architecture_implementation) {}

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr) override;
    void read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr) override;

    void write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) override;
    void write_regs(uint32_t byte_addr, uint32_t word_len, const void* data) override;
    void read_regs(uint32_t byte_addr, uint32_t word_len, void* data) override;

    void wait_for_non_mmio_flush() override{};

    bool is_remote() override { return false; };

    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index,
        tt_xy_pair start,
        tt_xy_pair end,
        std::uint64_t address,
        bool multicast,
        std::uint64_t ordering);
    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index, tt_xy_pair target, std::uint64_t address, std::uint64_t ordering = tlb_data::Relaxed);
    dynamic_tlb set_dynamic_tlb_broadcast(
        unsigned int tlb_index,
        std::uint64_t address,
        tt_xy_pair start,
        tt_xy_pair end,
        std::uint64_t ordering = tlb_data::Relaxed);

    void detect_hang_read(uint32_t data_read = HANG_READ_VALUE);

private:
    LockManager& lock_manager;
    PCIDevice& pci_device;
    architecture_implementation& architecture_implementation;

    // Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
    // Both routines assume that misaligned accesses are permitted on host memory.
    //
    // 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
    // which glibc's memcpy may perform when unrolling. This affects from and to device.
    // 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
    // to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
    void memcpy_to_device(void* dest, const void* src, std::size_t num_bytes);
    void memcpy_from_device(void* dest, const void* src, std::size_t num_bytes);

    void write_tlb_reg(
        uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size);

    bool is_hardware_hung();
};

}  // namespace tt::umd
