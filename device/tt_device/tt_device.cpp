// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/tt_device.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "utils.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#endif

#define LOAD_STREAM_32()                                                                 \
    do {                                                                                 \
        _mm256_stream_si256((__m256i *)dst8, _mm256_loadu_si256((const __m256i *)src8)); \
        src8 += sizeof(__m256i);                                                         \
        dst8 += sizeof(__m256i);                                                         \
    } while (0)

#define LOAD_STREAM_16()                                                           \
    do {                                                                           \
        _mm_stream_si128((__m128i *)dst8, _mm_loadu_si128((const __m128i *)src8)); \
        src8 += sizeof(__m128i);                                                   \
        dst8 += sizeof(__m128i);                                                   \
    } while (0)

#define LOAD_STREAM_4()                                     \
    do {                                                    \
        _mm_stream_si32((int32_t *)dst8, *(int32_t *)src8); \
        src8 += sizeof(int32_t);                            \
        dst8 += sizeof(int32_t);                            \
    } while (0)

#define LOAD_STREAM_4_UNALIGNED()                 \
    do {                                          \
        int32_t val = 0;                          \
        std::memcpy(&val, src8, sizeof(int32_t)); \
        _mm_stream_si32((int32_t *)dst8, val);    \
        src8 += sizeof(int32_t);                  \
        dst8 += sizeof(int32_t);                  \
    } while (0)

static constexpr uint32_t MEMCPY_ALIGNMENT = 16;

// TODO #526: This is a hack to allow UMD to use the NOC1 TLB.
bool umd_use_noc1 = false;

namespace tt::umd {

void TTDevice::use_noc1(bool use_noc1) { umd_use_noc1 = use_noc1; }

TTDevice::TTDevice(
    std::shared_ptr<PCIDevice> pci_device, std::unique_ptr<architecture_implementation> architecture_impl) :
    pci_device_(pci_device),
    communication_device_type_(IODeviceType::PCIe),
    communication_device_id_(pci_device_->get_device_num()),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    lock_manager.initialize_mutex(MutexType::TT_DEVICE_IO, get_communication_device_id());
}

TTDevice::TTDevice(
    std::shared_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    std::unique_ptr<architecture_implementation> architecture_impl) :
    jtag_device_(jtag_device),
    communication_device_type_(IODeviceType::JTAG),
    communication_device_id_(jlink_id),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    lock_manager.initialize_mutex(MutexType::TT_DEVICE_IO, get_communication_device_id(), IODeviceType::JTAG);
}

TTDevice::TTDevice() {}

TTDevice::TTDevice(std::unique_ptr<architecture_implementation> architecture_impl) :
    architecture_impl_(std::move(architecture_impl)), arch(architecture_impl_->get_architecture()) {}

void TTDevice::init_tt_device(const std::chrono::milliseconds timeout_ms) {
    pre_init_hook();
    if (!wait_arc_core_start(timeout_ms)) {
        throw std::runtime_error(fmt::format(
            "Timed out after waiting {} ms for arc core ({}, {}) to start", timeout_ms, arc_core.x, arc_core.y));
    }
    arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    telemetry = ArcTelemetryReader::create_arc_telemetry_reader(this);
    firmware_info_provider = FirmwareInfoProvider::create_firmware_info_provider(this);
    post_init_hook();
}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(int device_number, IODeviceType device_type) {
    // TODO make abstract IO handler inside TTDevice.
    if (device_type == IODeviceType::JTAG) {
        auto jtag_device = JtagDevice::create();

        switch (jtag_device->get_jtag_arch(device_number)) {
            case ARCH::WORMHOLE_B0:
                return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(jtag_device, device_number));
            case ARCH::BLACKHOLE:
                return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(jtag_device, device_number));
            default:
                return nullptr;
        }
    }

    auto pci_device = std::make_shared<PCIDevice>(device_number);

    switch (pci_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(pci_device));
        case ARCH::BLACKHOLE:
            return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(pci_device));
        default:
            return nullptr;
    }
}

std::unique_ptr<TTDevice> TTDevice::create(std::unique_ptr<RemoteCommunication> remote_communication) {
    switch (remote_communication->get_local_device()->get_arch()) {
        case tt::ARCH::WORMHOLE_B0: {
            // This is a workaround to allow RemoteWormholeTTDevice creation over JTAG.
            // TODO: In the future, either remove this if branch or refactor the RemoteWormholeTTDevice class hierarchy.
            if (remote_communication->get_local_device()->get_communication_device_type() == IODeviceType::JTAG) {
                return std::unique_ptr<RemoteWormholeTTDevice>(
                    new RemoteWormholeTTDevice(std::move(remote_communication), IODeviceType::JTAG));
            }
            return std::unique_ptr<RemoteWormholeTTDevice>(new RemoteWormholeTTDevice(std::move(remote_communication)));
        }
        case tt::ARCH::BLACKHOLE: {
            if (remote_communication->get_local_device()->get_communication_device_type() == IODeviceType::JTAG) {
                TT_THROW("Remote TTDevice creation over JTAG is not yet supported for Blackhole architecture.");
            }
            return std::unique_ptr<RemoteBlackholeTTDevice>(
                new RemoteBlackholeTTDevice(std::move(remote_communication)));
        }
        default:
            throw std::runtime_error("Remote TTDevice creation is not supported for this architecture.");
    }
}

architecture_implementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

std::shared_ptr<PCIDevice> TTDevice::get_pci_device() { return pci_device_; }

std::shared_ptr<JtagDevice> TTDevice::get_jtag_device() { return jtag_device_; }

tt::ARCH TTDevice::get_arch() { return arch; }

void TTDevice::detect_hang_read(std::uint32_t data_read) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        // Jtag protocol uses different communication paths from pci therefore
        // there's no need to check hang which is in this case pci-specific.
        return;
    }
    if (data_read == HANG_READ_VALUE && is_hardware_hung()) {
        std::uint32_t scratch_data =
            *pci_device_->get_register_address<std::uint32_t>(architecture_impl_->get_read_checking_offset());

        throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
    }
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc
void TTDevice::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_regs is not applicable for JTAG communication type.");
    }
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void TTDevice::write_regs(uint32_t byte_addr, uint32_t word_len, const void *data) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_regs is not applicable for JTAG communication type.");
    }
    volatile uint32_t *dest = pci_device_->get_register_address<uint32_t>(byte_addr);
    const uint32_t *src = reinterpret_cast<const uint32_t *>(data);

    write_regs(dest, src, word_len);
}

void TTDevice::read_regs(uint32_t byte_addr, uint32_t word_len, void *data) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("read_regs is not applicable for JTAG communication type.");
    }
    const volatile uint32_t *src = pci_device_->get_register_address<uint32_t>(byte_addr);
    uint32_t *dest = reinterpret_cast<uint32_t *>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
}

void TTDevice::custom_memcpy(void *dst, const void *src, std::size_t size) {
    if (size == 0) {
        return;
    }

    uint8_t *d = static_cast<uint8_t *>(dst);
    const uint8_t *s = static_cast<const uint8_t *>(src);

    uintptr_t addr = reinterpret_cast<uintptr_t>(d);
    size_t misalign = addr % 16;

    if (misalign != 0) {
        size_t prefix = 16 - misalign;
        if (prefix > size) {
            prefix = size;
        }

        std::memcpy(d, s, prefix);
        d += prefix;
        s += prefix;
        size -= prefix;
    }

    size_t aligned_size = size & ~size_t(15);  // round down to multiple of 16
    if (aligned_size > 0) {
        custom_memcpy_aligned(d, s, aligned_size);
        d += aligned_size;
        s += aligned_size;
        size -= aligned_size;
    }

    if (size > 0) {
        std::memcpy(d, s, size);
    }
}

void TTDevice::custom_memcpy_aligned(void *dst, const void *src, std::size_t n) {
#if defined(__x86_64__) || defined(__i386__)
    // Ensure destination is properly aligned for optimal SIMD performance
    TT_ASSERT((uintptr_t)dst % MEMCPY_ALIGNMENT == 0);

    // Configuration for bulk processing: inner loop processes 8 x 32-byte operations
    // This creates 256-byte blocks (8 * 32 = 256 bytes) for maximum throughput
    constexpr uint32_t inner_loop = 8;
    constexpr uint32_t inner_blk_size = inner_loop * sizeof(__m256i);  // 256 bytes

    const auto *src8 = static_cast<const uint8_t *>(src);
    auto *dst8 = static_cast<uint8_t *>(dst);

    size_t num_lines = n / inner_blk_size;  // Number of 256-byte blocks to process

    // PHASE 1: Process 256-byte blocks 32 bytes at a time
    // This is the main bulk processing phase for maximum efficiency
    if (num_lines > 0) {
        // Handle potential misalignment by processing a single 16-byte chunk first
        // This ensures subsequent 32-byte operations are properly aligned
        // WARNING: This does not cover the case where dst is not 16-byte aligned
        if ((uintptr_t)dst8 % sizeof(__m256i) != 0) {
            LOAD_STREAM_16();
            n -= sizeof(__m128i);
            num_lines = n / inner_blk_size;  // Recalculate after alignment adjustment
        }

        // Main bulk processing loop: Each iteration processes a 256 byte block. Blocks are processed 32 bytes at a
        // time.
        for (size_t i = 0; i < num_lines; ++i) {
            for (size_t j = 0; j < inner_loop; ++j) {
                LOAD_STREAM_32();
            }
            n -= inner_blk_size;
        }
    }

    // PHASE 2: Process remaining data that doesn't fill a complete 256-byte block
    if (n > 0) {
        // Phase 2.1: Process remaining 32-byte chunks
        num_lines = n / sizeof(__m256i);  // Number of 32-byte blocks to process
        if (num_lines > 0) {
            // Handle alignment for 32-byte operations if needed
            // WARNING: This does not cover the case where dst is not 16-byte aligned
            if ((uintptr_t)dst8 % sizeof(__m256i) != 0) {
                LOAD_STREAM_16();
                n -= sizeof(__m128i);
                num_lines = n / sizeof(__m256i);  // Recalculate after alignment adjustment
            }

            // Process individual 32-byte blocks
            for (size_t i = 0; i < num_lines; ++i) {
                LOAD_STREAM_32();
            }
            n -= num_lines * sizeof(__m256i);
        }

        // PHASE 2.2: Process remaining 16-byte chunks
        num_lines = n / sizeof(__m128i);  // Number of 16-byte blocks to process
        if (num_lines > 0) {
            for (size_t i = 0; i < num_lines; ++i) {
                LOAD_STREAM_16();
            }
            n -= num_lines * sizeof(__m128i);
        }

        // PHASE 2.3: Process remaining 4-byte chunks
        num_lines = n / sizeof(int32_t);  // Number of 4-byte blocks to process
        if (num_lines > 0) {
            if ((uintptr_t)src8 % sizeof(int32_t) != 0) {
                for (size_t i = 0; i < num_lines; ++i) {
                    LOAD_STREAM_4_UNALIGNED();
                }
            } else {
                for (size_t i = 0; i < num_lines; ++i) {
                    LOAD_STREAM_4();
                }
            }
            n -= num_lines * sizeof(int32_t);
        }

        // PHASE 2.4: Handle the final few bytes (< 4 bytes)
        // We are the ones in control of and allocating the dst buffer,
        // so writing a few bytes extra is okay because we are guaranteeing the size is adequate.
        if (n > 0) {
            int32_t val = 0;
            std::memcpy(&val, src8, n);
            _mm_stream_si32((int32_t *)dst8, val);
        }
    }

    // Optional memory fence for debugging/synchronization
    // Ensures all streaming stores complete before function returns
    // if constexpr (debug_sync) {
    tt_driver_atomics::sfence();
// }
#else
    std::memcpy(dest, src, num_bytes);
#endif
}

void TTDevice::memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("memcpy_to_device is not applicable for JTAG communication type.");
    }
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

        custom_memcpy(reinterpret_cast<char *>(&tmp) + dest_misalignment, src, leading_len);
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

        custom_memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

void TTDevice::memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("memcpy_from_device is not applicable for JTAG communication type.");
    }
    typedef std::uint32_t copy_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t *>(src_addr - src_misalignment);

        copy_t tmp = *sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        custom_memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
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
        custom_memcpy(dp, &tmp, trailing_len);
    }
}

void TTDevice::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t *buffer_addr) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_block is not applicable for JTAG communication type.");
    }
    void *dest = nullptr;
    if (pci_device_->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        dest = reinterpret_cast<uint8_t *>(pci_device_->bar4_wc) + byte_addr;
    } else {
        dest = pci_device_->get_register_address<uint8_t>(byte_addr);
    }

    const void *src = reinterpret_cast<const void *>(buffer_addr);
    bool use_safe_memcpy = false;
    if constexpr (is_arm_platform() || is_riscv_platform()) {
        use_safe_memcpy = true;
    } else {
        use_safe_memcpy = (arch == tt::ARCH::WORMHOLE_B0);
    }
    if (use_safe_memcpy) {
        memcpy_to_device(dest, src, num_bytes);
    } else {
        custom_memcpy(dest, src, num_bytes);
    }
}

void TTDevice::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t *buffer_addr) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("read_block is not applicable for JTAG communication type.");
    }
    void *src = nullptr;
    if (pci_device_->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        src = reinterpret_cast<uint8_t *>(pci_device_->bar4_wc) + byte_addr;
    } else {
        src = pci_device_->get_register_address<uint8_t>(byte_addr);
    }

    void *dest = reinterpret_cast<void *>(buffer_addr);
    bool use_safe_memcpy = false;
    if constexpr (is_arm_platform() || is_riscv_platform()) {
        use_safe_memcpy = true;
    } else {
        use_safe_memcpy = (arch == tt::ARCH::WORMHOLE_B0);
    }
    if (use_safe_memcpy) {
        memcpy_from_device(dest, src, num_bytes);
    } else {
        custom_memcpy(dest, src, num_bytes);
    }

    if (num_bytes >= sizeof(std::uint32_t)) {
        // TODO: looks like there is potential for undefined behavior here.
        detect_hang_read(*reinterpret_cast<std::uint32_t *>(dest));
    }
}

void TTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(communication_device_id_, mem_ptr, core.x, core.y, addr, size, umd_use_noc1 ? 1 : 0);
        return;
    }
    auto lock = lock_manager.acquire_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num());
    uint8_t *buffer_addr = static_cast<uint8_t *>(mem_ptr);
    const uint32_t tlb_index = get_architecture_implementation()->get_reg_tlb();
    while (size > 0) {
        auto [mapped_address, tlb_size] = set_dynamic_tlb(tlb_index, core, addr, tlb_data::Strict);
        uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
        read_block(mapped_address, transfer_size, buffer_addr);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

void TTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(communication_device_id_, mem_ptr, core.x, core.y, addr, size, umd_use_noc1 ? 1 : 0);
        return;
    }
    auto lock = lock_manager.acquire_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num());
    const uint8_t *buffer_addr = static_cast<const uint8_t *>(mem_ptr);
    const uint32_t tlb_index = get_architecture_implementation()->get_reg_tlb();

    while (size > 0) {
        auto [mapped_address, tlb_size] = set_dynamic_tlb(tlb_index, core, addr, tlb_data::Strict);
        uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
        write_block(mapped_address, transfer_size, buffer_addr);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

void TTDevice::write_tlb_reg(
    uint32_t byte_addr, uint64_t value_lower, uint64_t value_upper, uint32_t tlb_cfg_reg_size) {
    TT_ASSERT(
        (tlb_cfg_reg_size == 8) or (tlb_cfg_reg_size == 12),
        "Tenstorrent hardware supports only 64bit or 96bit TLB config regs");

    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_tlb_reg is not applicable for JTAG communication type.");
    }

    volatile uint32_t *dest_dw = pci_device_->get_register_address<uint32_t>(byte_addr);
    volatile uint32_t *dest_extra_dw = pci_device_->get_register_address<uint32_t>(byte_addr + 8);

    // The store below goes through UC memory on x86, which has implicit ordering constraints with WC accesses.
    // ARM has no concept of UC memory. This will not allow for implicit ordering of this store wrt other memory
    // accesses. Insert an explicit full memory barrier for ARM. Do the same for RISC-V.
    if constexpr (is_arm_platform() || is_riscv_platform()) {
        tt_driver_atomics::mfence();
    }
    dest_dw[0] = static_cast<uint32_t>(value_lower);
    dest_dw[1] = static_cast<uint32_t>(value_lower >> 32);
    if (tlb_cfg_reg_size > 8) {
        uint32_t *p_value_upper = reinterpret_cast<uint32_t *>(&value_upper);
        *dest_extra_dw = p_value_upper[0];
    }
    tt_driver_atomics::mfence();  // Otherwise subsequent WC loads move earlier than the above UC store to the TLB
                                  // register.
}

// Get TLB index (from zero), check if it's in 16MB, 2MB or 1MB TLB range, and dynamically program it.
dynamic_tlb TTDevice::set_dynamic_tlb(
    unsigned int tlb_index,
    tt_xy_pair start,
    tt_xy_pair end,
    std::uint64_t address,
    bool multicast,
    std::uint64_t ordering) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("set_dynamic_tlb is not applicable for JTAG communication type.");
    }
    if (multicast) {
        std::tie(start, end) = architecture_impl_->multicast_workaround(start, end);
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

    tlb_configuration tlb_config = architecture_impl_->get_tlb_configuration(tlb_index);
    std::uint32_t TLB_CFG_REG_SIZE_BYTES = architecture_impl_->get_tlb_cfg_reg_size_bytes();
    uint64_t tlb_address = address / tlb_config.size;
    uint32_t local_address = address % tlb_config.size;
    uint64_t tlb_base = tlb_config.base + (tlb_config.size * tlb_config.index_offset);
    uint32_t tlb_cfg_reg = tlb_config.cfg_addr + (TLB_CFG_REG_SIZE_BYTES * tlb_config.index_offset);

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

dynamic_tlb TTDevice::set_dynamic_tlb(
    unsigned int tlb_index, tt_xy_pair target, std::uint64_t address, std::uint64_t ordering) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("set_dynamic_tlb is not applicable for JTAG communication type.");
    }
    return set_dynamic_tlb(tlb_index, tt_xy_pair(0, 0), target, address, false, ordering);
}

dynamic_tlb TTDevice::set_dynamic_tlb_broadcast(
    unsigned int tlb_index, std::uint64_t address, tt_xy_pair start, tt_xy_pair end, std::uint64_t ordering) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("set_dynamic_tlb_broadcast is not applicable for JTAG communication type.");
    }
    // Issue a broadcast to cores included in the start (top left) and end (bottom right) grid
    return set_dynamic_tlb(tlb_index, start, end, address, true, ordering);
}

void TTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    throw std::runtime_error("configure_iatu_region is not implemented for this device");
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    if (dram_channel >= architecture_impl_->get_dram_banks_number()) {
        throw std::runtime_error(fmt::format(
            "Invalid DRAM channel index {}, maximum index for given architecture is {}",
            dram_channel,
            architecture_impl_->get_dram_banks_number() - 1));
    }
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status =
            get_firmware_info_provider()->get_dram_training_status(architecture_impl_->get_dram_banks_number());

        if (dram_training_status.empty()) {
            log_warning(LogUMD, "DRAM training status is not available, breaking the wait for DRAM training.");
            return;
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::FAIL) {
            throw std::runtime_error("DRAM training failed");
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::SUCCESS) {
            return;
        }

        utils::check_timeout(
            start,
            timeout_ms,
            fmt::format("DRAM training for channel {} timed out after {} ms", dram_channel, timeout_ms));
    }
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("bar_write32 is not applicable for JTAG communication type.");
    }
    if (addr < get_pci_device()->bar0_uc_offset) {
        write_block(addr, sizeof(data), reinterpret_cast<const uint8_t *>(&data));  // do we have to reinterpret_cast?
    } else {
        write_regs(addr, 1, &data);
    }
}

uint32_t TTDevice::bar_read32(uint32_t addr) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("bar_read32 is not applicable for JTAG communication type.");
    }
    uint32_t data;
    if (addr < get_pci_device()->bar0_uc_offset) {
        read_block(addr, sizeof(data), reinterpret_cast<uint8_t *>(&data));
    } else {
        read_regs(addr, 1, &data);
    }
    return data;
}

ArcMessenger *TTDevice::get_arc_messenger() const { return arc_messenger_.get(); }

ArcTelemetryReader *TTDevice::get_arc_telemetry_reader() const { return telemetry.get(); }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return firmware_info_provider.get(); }

semver_t TTDevice::get_firmware_version() { return get_firmware_info_provider()->get_firmware_version(); }

TTDevice::~TTDevice() {
    // Remote TTDevices actually use TTDevice from remote_communication object.
    // Even though they extend TTDevices, they don't actually act as TTDevices but
    // as wrappers around remote communication which contain TTDevices.
    // Therefore, they don't need to clear mutexes since they don't use them directly.
    // Moreover, if they would try to clear mutexes, it would cause errors since Remote TTDevice
    // didn't initialize its own PCIDevice which is needed to identify the right mutex.
    if (is_remote_tt_device) {
        return;
    }
    lock_manager.clear_mutex(MutexType::TT_DEVICE_IO, get_communication_device_id(), communication_device_type_);
}

void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_remote() { return is_remote_tt_device; }

int TTDevice::get_communication_device_id() const { return communication_device_id_; }

IODeviceType TTDevice::get_communication_device_type() const { return communication_device_type_; }

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

uint64_t TTDevice::get_refclk_counter() {
    uint32_t high1_addr = 0, high2_addr = 0, low_addr = 0;
    read_from_arc_apb(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    read_from_arc_apb(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    read_from_arc_apb(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    if (high2_addr > high1_addr) {
        read_from_arc_apb(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

uint64_t TTDevice::get_board_id() { return get_firmware_info_provider()->get_board_id(); }

double TTDevice::get_asic_temperature() { return get_firmware_info_provider()->get_asic_temperature(); }

uint8_t TTDevice::get_asic_location() { return get_firmware_info_provider()->get_asic_location(); }

ChipInfo TTDevice::get_chip_info() {
    ChipInfo chip_info;

    chip_info.noc_translation_enabled = get_noc_translation_enabled();
    chip_info.board_id = get_board_id();
    chip_info.board_type = get_board_type();
    chip_info.asic_location = get_asic_location();

    return chip_info;
}

uint32_t TTDevice::get_max_clock_freq() { return get_firmware_info_provider()->get_max_clock_freq(); }

uint32_t TTDevice::get_risc_reset_state(tt_xy_pair core) {
    uint32_t tensix_risc_state;
    read_from_device(&tensix_risc_state, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));

    return tensix_risc_state;
}

void TTDevice::set_risc_reset_state(tt_xy_pair core, const uint32_t risc_flags) {
    write_to_device(&risc_flags, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    tt_driver_atomics::sfence();
}

tt_xy_pair TTDevice::get_arc_core() const { return arc_core; }

TlbWindow *TTDevice::get_cached_tlb_window(tlb_data config) {
    if (cached_tlb_window == nullptr) {
        cached_tlb_window =
            std::make_unique<TlbWindow>(get_pci_device()->allocate_tlb(1 << 21, TlbMapping::UC), config);
        return cached_tlb_window.get();
    }
    cached_tlb_window->configure(config);
    return cached_tlb_window.get();
}

void TTDevice::noc_multicast_write(void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        throw std::runtime_error("noc_multicast_write is not applicable for JTAG communication type.");
    }
    auto lock = lock_manager.acquire_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num());
    uint8_t *buffer_addr = static_cast<uint8_t *>(dst);
    const uint32_t tlb_index = get_architecture_implementation()->get_reg_tlb();

    while (size > 0) {
        auto [mapped_address, tlb_size] =
            set_dynamic_tlb_broadcast(tlb_index, addr, core_start, core_end, tlb_data::Strict);
        uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
        write_block(mapped_address, transfer_size, buffer_addr);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

}  // namespace tt::umd
