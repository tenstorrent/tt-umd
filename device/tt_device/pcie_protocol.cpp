/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/pcie_protocol.hpp"

#include "api/umd/device/driver_atomics.hpp"
#include "assert.hpp"
#include "tt-logger/tt-logger.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

// TODO: should be removed from tt_device.h, and put into blackhole_tt_device.h
// TODO: this is a bit of a hack... something to revisit when we formalize an
// abstraction for IO.
// BAR0 size for Blackhole, used to determine whether write block should use BAR0 or BAR4
static constexpr uint64_t BAR0_BH_SIZE = 512 * 1024 * 1024;

void PcieProtocol::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    auto lock = lock_manager_.acquire_mutex(MutexType::TT_DEVICE_IO, pci_device_->get_device_num());
    uint8_t *buffer_addr = (uint8_t *)(uintptr_t)(mem_ptr);
    const uint32_t tlb_index = architecture_implementation_.get_reg_tlb();

    while (size > 0) {
        auto [mapped_address, tlb_size] = set_dynamic_tlb(tlb_index, core, addr, tlb_data::Strict);
        uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
        write_block(mapped_address, transfer_size, buffer_addr);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

void PcieProtocol::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    auto lock = lock_manager_.acquire_mutex(MutexType::TT_DEVICE_IO, pci_device_->get_device_num());
    uint8_t *buffer_addr = static_cast<uint8_t *>(mem_ptr);
    const uint32_t tlb_index = architecture_implementation_.get_reg_tlb();
    while (size > 0) {
        auto [mapped_address, tlb_size] = set_dynamic_tlb(tlb_index, core, addr, tlb_data::Strict);
        uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
        read_block(mapped_address, transfer_size, buffer_addr);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

void PcieProtocol::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t *buffer_addr) {
    void *dest = nullptr;
    if (pci_device_->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        dest = reinterpret_cast<uint8_t *>(pci_device_->bar4_wc) + byte_addr;
    } else {
        dest = pci_device_->get_register_address<uint8_t>(byte_addr);
    }

    const void *src = reinterpret_cast<const void *>(buffer_addr);
    if (architecture_implementation_.get_architecture() == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device(dest, src, num_bytes);
    } else {
        memcpy(dest, src, num_bytes);
    }
}

void PcieProtocol::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t *buffer_addr) {
    void *src = nullptr;
    if (pci_device_->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        src = reinterpret_cast<uint8_t *>(pci_device_->bar4_wc) + byte_addr;
    } else {
        src = pci_device_->get_register_address<uint8_t>(byte_addr);
    }

    void *dest = reinterpret_cast<void *>(buffer_addr);
    if (architecture_implementation_.get_architecture() == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(dest, src, num_bytes);
    } else {
        memcpy(dest, src, num_bytes);
    }

    if (num_bytes >= sizeof(std::uint32_t)) {
        detect_hang_read(*reinterpret_cast<std::uint32_t *>(dest));
    }
}

void PcieProtocol::memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the destination (device) pointer. If needed, do RMW to fix up the
    // first partial word.
    volatile copy_t *dp;

    std::uintptr_t dest_addr = reinterpret_cast<std::uintptr_t>(dest);
    unsigned int dest_misalignment = dest_addr % sizeof(copy_t);

    if (dest_misalignment != 0) {
        // Read-modify-write for the first dest element.
        dp = reinterpret_cast<copy_t *>(dest_addr - dest_misalignment);

        copy_t tmp = *dp;

        auto leading_len = std::min(sizeof(tmp) - dest_misalignment, num_bytes);

        std::memcpy(reinterpret_cast<char *>(&tmp) + dest_misalignment, src, leading_len);
        num_bytes -= leading_len;
        src = static_cast<const char *>(src) + leading_len;

        *dp++ = tmp;

    } else {
        dp = static_cast<copy_t *>(dest);
    }

    // Copy the destination-aligned middle.
    const copy_t *sp = static_cast<const copy_t *>(src);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer, again RMW on the destination.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *dp;

        std::memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

void PcieProtocol::memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t *>(src_addr - src_misalignment);

        copy_t tmp = *sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        std::memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
        num_bytes -= leading_len;
        dest = static_cast<char *>(dest) + leading_len;

    } else {
        sp = static_cast<const volatile copy_t *>(src);
    }

    // Copy the source-aligned middle.
    copy_t *dp = static_cast<copy_t *>(dest);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *sp;
        std::memcpy(dp, &tmp, trailing_len);
    }
}

void PcieProtocol::write_tlb_reg(
    uint32_t byte_addr, uint64_t value_lower, uint64_t value_upper, uint32_t tlb_cfg_reg_size) {
    TT_ASSERT(
        (tlb_cfg_reg_size == 8) or (tlb_cfg_reg_size == 12),
        "Tenstorrent hardware supports only 64bit or 96bit TLB config regs");

    volatile uint64_t *dest_qw = pci_device_->get_register_address<uint64_t>(byte_addr);
    volatile uint32_t *dest_extra_dw = pci_device_->get_register_address<uint32_t>(byte_addr + 8);
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
}

// Get TLB index (from zero), check if it's in 16MB, 2MB or 1MB TLB range, and dynamically program it.
dynamic_tlb PcieProtocol::set_dynamic_tlb(
    unsigned int tlb_index,
    tt_xy_pair start,
    tt_xy_pair end,
    std::uint64_t address,
    bool multicast,
    std::uint64_t ordering) {
    // if (communication_device_type_ == IODeviceType::JTAG) {
    //     TT_THROW("set_dynamic_tlb is not applicable for JTAG communication type.");
    // }
    if (multicast) {
        std::tie(start, end) = architecture_implementation_.multicast_workaround(start, end);
    }

    log_trace(
        LogUMD,
        "set_dynamic_tlb with arguments: tlb_index = {}, start = ({}, {}), end = ({}, {}), address = 0x{:x}, "
        "multicast "
        "= {}, ordering = {}",
        tlb_index,
        start.x,
        start.y,
        end.x,
        end.y,
        address,
        multicast,
        (int)ordering);

    tlb_configuration tlb_config = architecture_implementation_.get_tlb_configuration(tlb_index);
    std::uint32_t TLB_CFG_REG_SIZE_BYTES = architecture_implementation_.get_tlb_cfg_reg_size_bytes();
    uint64_t tlb_address = address / tlb_config.size;
    uint32_t local_address = address % tlb_config.size;
    uint64_t tlb_base = tlb_config.base + (tlb_config.size * tlb_config.index_offset);
    uint32_t tlb_cfg_reg = tlb_config.cfg_addr + (TLB_CFG_REG_SIZE_BYTES * tlb_config.index_offset);
    auto arch = architecture_implementation_.get_architecture();

    std::pair<std::uint64_t, std::uint64_t> tlb_reg_config =
        tlb_data{
            .local_offset = tlb_address,
            .x_end = static_cast<uint64_t>(end.x),
            .y_end = static_cast<uint64_t>(end.y),
            .x_start = static_cast<uint64_t>(start.x),
            .y_start = static_cast<uint64_t>(start.y),
            .noc_sel = umd_use_noc1 ? 1U : 0,
            .mcast = multicast,
            .ordering = ordering,
            // TODO #2715: hack for Blackhole A0, will potentially be fixed in B0.
            // Using the same static vc for reads and writes through TLBs can hang the card. It doesn't even have to
            // be the same TLB. Dynamic vc should not have this issue. There might be a perf impact with using
            // dynamic vc.
            .static_vc = (arch == tt::ARCH::BLACKHOLE) ? false : true,
        }
            .apply_offset(tlb_config.offset);

    log_trace(
        LogUMD,
        "set_dynamic_tlb() with tlb_index: {} tlb_index_offset: {} dynamic_tlb_size: {}MB tlb_base: 0x{:x} "
        "tlb_cfg_reg: 0x{:x} to core ({},{})",
        tlb_index,
        tlb_config.index_offset,
        tlb_config.size / (1024 * 1024),
        tlb_base,
        tlb_cfg_reg,
        end.x,
        end.y);
    write_tlb_reg(tlb_cfg_reg, tlb_reg_config.first, tlb_reg_config.second, TLB_CFG_REG_SIZE_BYTES);

    return {tlb_base + local_address, tlb_config.size - local_address};
}

dynamic_tlb PcieProtocol::set_dynamic_tlb(
    unsigned int tlb_index, tt_xy_pair target, std::uint64_t address, std::uint64_t ordering) {
    return set_dynamic_tlb(tlb_index, tt_xy_pair(0, 0), target, address, false, ordering);
}

void PcieProtocol::detect_hang_read(std::uint32_t data_read) {
    if (data_read == HANG_READ_VALUE && is_hardware_hung()) {
        std::uint32_t scratch_data =
            *pci_device_->get_register_address<std::uint32_t>(architecture_implementation_.get_read_checking_offset());

        throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
    }
}

bool PcieProtocol::is_hardware_hung() {
    volatile const void *addr = reinterpret_cast<const char *>(pci_device_->bar0_uc) +
                                (architecture_implementation_.get_arc_axi_apb_peripheral_offset() +
                                 architecture_implementation_.get_arc_reset_scratch_offset() + 6 * 4) -
                                pci_device_->bar0_uc_offset;
    std::uint32_t scratch_data = *reinterpret_cast<const volatile std::uint32_t *>(addr);

    return (scratch_data == HANG_READ_VALUE);
}

}  // namespace tt::umd
