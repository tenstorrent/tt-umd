/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

class PcieProtocol : public DeviceProtocol {
public:
    PcieProtocol(PCIDevice* pci_device, architecture_implementation& architecture_implementation) :
        pci_device_(pci_device), architecture_implementation_(architecture_implementation) {
        lock_manager_.initialize_mutex(MutexType::TT_DEVICE_IO, pci_device_->get_device_num(), IODeviceType::PCIe);
    }

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    // PCIe specific methods.
    void write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr);
    void read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr);

    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index,
        tt_xy_pair start,
        tt_xy_pair end,
        std::uint64_t address,
        bool multicast,
        std::uint64_t ordering);
    dynamic_tlb set_dynamic_tlb(
        unsigned int tlb_index, tt_xy_pair target, std::uint64_t address, std::uint64_t ordering = tlb_data::Relaxed);

    void detect_hang_read(uint32_t data_read = HANG_READ_VALUE);

    void write_tlb_reg(
        uint32_t byte_addr, std::uint64_t value_lower, std::uint64_t value_upper, std::uint32_t tlb_cfg_reg_size);

    // Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
    // Both routines assume that misaligned accesses are permitted on host memory.
    //
    // 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
    // which glibc's memcpy may perform when unrolling. This affects from and to device.
    // 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
    // to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
    void memcpy_to_device(void* dest, const void* src, std::size_t num_bytes);
    void memcpy_from_device(void* dest, const void* src, std::size_t num_bytes);

    bool is_hardware_hung();

private:
    LockManager lock_manager_;
    PCIDevice* pci_device_;
    architecture_implementation& architecture_implementation_;
};

}  // namespace tt::umd
