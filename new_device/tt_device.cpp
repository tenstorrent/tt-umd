#include "tt_device.h"
#include "common/logger.hpp"

#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>

#include "wormhole_tt_device.h"

#include <mutex>
#include <cstdarg>
#include <cstring>

#include <fcntl.h>

using namespace boost::interprocess;

bool g_READ_CHECKING_ENABLED = true;
// Print all buffers smaller than this number of bytes
constexpr uint32_t g_NUM_BYTES_TO_PRINT = 8;

static const uint32_t MSG_ERROR_REPLY = 0xFFFFFFFF;
#define RED "\e[0;31m"
#define YEL "\e[0;33m"
#define RST "\e[0m"
#define LOG1(...) clr_printf("", __VA_ARGS__)  // Mostly debugging
#define LOG2(...) clr_printf("", __VA_ARGS__)  // Mostly debugging
#define WARN(...)  clr_printf(YEL, __VA_ARGS__)                       // Something wrong
#define ERROR(...) clr_printf(RED, __VA_ARGS__)                       // Something very bad

inline void clr_printf(const char *clr, const char *fmt, ...) {
    va_list args; va_start(args, fmt); printf ("%s", clr); vprintf(fmt, args); printf (RST); va_end(args);
}

inline void record_access (const char* where, uint32_t addr, uint32_t size, bool turbo, bool write, bool block, bool endline) {
    LOG2 ("%s PCI_ACCESS %s 0x%8x  %8d bytes %s %s%s", where, write ? "WR" : "RD", addr, size, turbo ? "TU" : "  ", block ? "BLK" : "   ", endline ? "\n" : "" );
}


namespace tt::umd {

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

// Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
// Both routines assume that misaligned accesses are permitted on host memory.
//
// 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
// which glibc's memcpy may perform when unrolling. This affects from and to device.
// 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
// to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.

void memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
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

    for (std::size_t i = 0; i < num_words; i++) *dp++ = *sp++;

    // Finally copy any sub-word trailer, again RMW on the destination.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *dp;

        std::memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

void memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
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

    for (std::size_t i = 0; i < num_words; i++) *dp++ = *sp++;

    // Finally copy any sub-word trailer.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *sp;
        std::memcpy(dp, &tmp, trailing_len);
    }
}


std::unique_ptr<TTDevice> TTDevice::open(unsigned int device_id) {
    device_id = device_id;
    std::unique_ptr<PCIDevice> pci_device = std::make_unique<PCIDevice>(device_id);

    switch (pci_device->get_arch()) {
        // case architecture::blackhole: return std::make_unique<blackhole_implementation>();
        // case architecture::grayskull: return std::make_unique<grayskull_implementation>();
        // case architecture::wormhole:
        case Arch::WORMHOLE: return std::make_unique<WormholeTTDevice>(std::move(pci_device));
        default: return nullptr;
    }
}

TTDevice::TTDevice(std::unique_ptr<PCIDevice> pci_device) : pci_device(std::move(pci_device)) {}

void TTDevice::print_device_info() {
    // device ID with which device was opened
    LOG1("PCIEIntfId   0x%x\n", device_id);
    LOG1("VID:DID      0x%x:0x%x\n", pci_device->device_info.vendor_id, pci_device->device_info.device_id);
    LOG1("SubVID:SubID 0x%x:0x%x\n", pci_device->device_info.subsystem_vendor_id, pci_device->device_info.subsystem_id);
    LOG1("BSF          %x:%x:%x\n", pci_device->pci_bus, pci_device->pci_device, pci_device->pci_function);
    LOG1("BAR          0x%llx  size: %dMB\n", pci_device->read_bar0_base(), pci_device->bar0_uc_size / 1024 / 1024);

}

// Get TLB index (from zero), check if it's in 16MB, 2MB or 1MB TLB range, and dynamically program it.
dynamic_tlb TTDevice::set_dynamic_tlb(
    unsigned int tlb_index,
    xy_pair start,
    xy_pair end,
    std::uint64_t address,
    bool multicast,
    std::unordered_map<chip_id_t, std::unordered_map<xy_pair, xy_pair>>& harvested_coord_translation,
    std::uint64_t ordering) {
    if (multicast) {
        std::tie(start, end) = multicast_workaround(start, end);
    }

    LOG2(
        "set_dynamic_tlb with arguments: tlb_index = %d, start = (%d, %d), end = (%d, %d), address = 0x%x, multicast = "
        "%d, ordering = %d\n",
        tlb_index,
        start.x,
        start.y,
        end.x,
        end.y,
        address,
        multicast,
        (int)ordering);

    tt::umd::tlb_configuration tlb_config = get_tlb_configuration(tlb_index);
    std::uint32_t TLB_CFG_REG_SIZE_BYTES = get_tlb_cfg_reg_size_bytes();
    auto translated_start_coords = harvested_coord_translation.at(pci_device->logical_id).at(start);
    auto translated_end_coords = harvested_coord_translation.at(pci_device->logical_id).at(end);
    uint32_t tlb_address = address / tlb_config.size;
    uint32_t local_offset = address % tlb_config.size;
    uint64_t tlb_base = tlb_config.base + (tlb_config.size * tlb_config.index_offset);
    uint32_t tlb_cfg_reg = tlb_config.cfg_addr + (TLB_CFG_REG_SIZE_BYTES * tlb_config.index_offset);

    std::pair<std::uint64_t, std::uint64_t> tlb_data_pair =
        tlb_data{
            .local_offset = tlb_address,
            .x_end = static_cast<uint64_t>(translated_end_coords.x),
            .y_end = static_cast<uint64_t>(translated_end_coords.y),
            .x_start = static_cast<uint64_t>(translated_start_coords.x),
            .y_start = static_cast<uint64_t>(translated_start_coords.y),
            .mcast = multicast,
            .ordering = ordering,
            // TODO #2715: hack for Blackhole A0, will potentially be fixed in B0.
            // Using the same static vc for reads and writes through TLBs can hang the card. It doesn't even have to be
            // the same TLB. Dynamic vc should not have this issue. There might be a perf impact with using dynamic vc.
            .static_vc = (pci_device->arch == Arch::BLACKHOLE) ? false : true,
        }
            .apply_offset(tlb_config.offset);

    LOG1(
        "set_dynamic_tlb() with tlb_index: %d tlb_index_offset: %d dynamic_tlb_size: %dMB tlb_base: 0x%x tlb_cfg_reg: "
        "0x%x\n",
        tlb_index,
        tlb_config.index_offset,
        tlb_config.size / (1024 * 1024),
        tlb_base,
        tlb_cfg_reg);
    // write_regs(dev -> hdev, tlb_cfg_reg, 2, &tlb_data);
    pci_device->write_tlb_reg(tlb_cfg_reg, tlb_data_pair.first, tlb_data_pair.second, TLB_CFG_REG_SIZE_BYTES);

    return {tlb_base + local_offset, tlb_config.size - local_offset};
}

dynamic_tlb TTDevice::set_dynamic_tlb(
    unsigned int tlb_index,
    xy_pair target,
    std::uint64_t address,
    std::unordered_map<chip_id_t, std::unordered_map<xy_pair, xy_pair>>& harvested_coord_translation,
    std::uint64_t ordering) {
    return set_dynamic_tlb(tlb_index, xy_pair(0, 0), target, address, false, harvested_coord_translation, ordering);
}

dynamic_tlb TTDevice::set_dynamic_tlb_broadcast(
    unsigned int tlb_index,
    std::uint64_t address,
    std::unordered_map<chip_id_t, std::unordered_map<xy_pair, xy_pair>>& harvested_coord_translation,
    xy_pair start,
    xy_pair end,
    std::uint64_t ordering) {
    // Issue a broadcast to cores included in the start (top left) and end (bottom right) grid
    return set_dynamic_tlb(tlb_index, start, end, address, true, harvested_coord_translation, ordering);
}


bool TTDevice::is_hardware_hung() {
    volatile const void *addr = reinterpret_cast<const char *>(pci_device->bar0_uc) +
                                (get_arc_reset_scratch_offset() + 6 * 4) - pci_device->bar0_uc_offset;
    std::uint32_t scratch_data = *reinterpret_cast<const volatile std::uint32_t *>(addr);

    return (scratch_data == 0xffffffffu);
}

bool TTDevice::auto_reset_board() { return ((pci_device->reset_by_ioctl() || pci_device->reset_by_sysfs()) && !is_hardware_hung()); }

void TTDevice::detect_ffffffff_read(std::uint32_t data_read) {
    if (g_READ_CHECKING_ENABLED && data_read == 0xffffffffu && is_hardware_hung()) {
        std::uint32_t scratch_data = *pci_device->register_address<std::uint32_t>(pci_device->read_checking_offset);

        if (auto_reset_board()) {
            throw std::runtime_error("Read 0xffffffff from PCIE: auto-reset succeeded.");
        } else {
            throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
        }
    }
}

void TTDevice::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t *buffer_addr) {
    record_access(
        "read_block_b", byte_addr, num_bytes, false, false, true, false);  // addr, size, turbo, write, block, endline

    void *reg_mapping = get_reg_mapping(byte_addr);

    const void *src = reinterpret_cast<const char *>(reg_mapping) + byte_addr;
    void *dest = reinterpret_cast<void *>(buffer_addr);

#ifndef DISABLE_ISSUE_3487_FIX
    memcpy_from_device(dest, src, num_bytes);
#else
#ifdef FAST_MEMCPY

    if ((num_bytes % 32 == 0) && ((intptr_t(dest) & 31) == 0) && ((intptr_t(src) & 31) == 0))
        memcpy_from_device(dest, src, num_bytes);
    {
        // Faster memcpy version.. about 8x currently compared to pci_read above
        fastMemcpy(dest, src, num_bytes);
    }
    else
#else
    // ~4x faster than pci_read above, but works for all sizes and alignments
    memcpy(dest, src, num_bytes);
#endif
#endif

    if (num_bytes >= sizeof(std::uint32_t)) {
        detect_ffffffff_read(*reinterpret_cast<std::uint32_t *>(dest));
    }
    print_buffer(buffer_addr, std::min((uint64_t)g_NUM_BYTES_TO_PRINT, num_bytes), true);
}

void TTDevice::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t *buffer_addr) {
    record_access(
        "write_block_b", byte_addr, num_bytes, false, true, true, false);  // addr, size, turbo, write, block, endline

    void *reg_mapping = get_reg_mapping(byte_addr);

    void *dest = reinterpret_cast<char *>(reg_mapping) + byte_addr;
    const void *src = reinterpret_cast<const void *>(buffer_addr);
#ifndef DISABLE_ISSUE_3487_FIX
    memcpy_to_device(dest, src, num_bytes);
#else
#ifdef FAST_MEMCPY
    memcpy_to_device(dest, src, num_bytes);
    if ((num_bytes % 32 == 0) && ((intptr_t(dest) & 31) == 0) && ((intptr_t(src) & 31) == 0))

    {
        // Faster memcpy version.. about 8x currently compared to pci_read above
        fastMemcpy(dest, src, num_bytes);
    } else
#else
    // ~4x faster than pci_read above, but works for all sizes and alignments
    memcpy(dest, src, num_bytes);
#endif
#endif
    print_buffer(buffer_addr, std::min((uint64_t)g_NUM_BYTES_TO_PRINT, num_bytes), true);
}


bool TTDevice::tensix_or_eth_in_broadcast(
    const std::set<uint32_t>& cols_to_exclude) {
    bool found_tensix_or_eth = false;
    for (const auto& col : get_t6_x_locations()) {
        found_tensix_or_eth |= (cols_to_exclude.find(col) == cols_to_exclude.end());
    }
    return found_tensix_or_eth;
}

bool TTDevice::valid_tensix_broadcast_grid(
    const std::set<uint32_t>& rows_to_exclude,
    const std::set<uint32_t>& cols_to_exclude) {
    bool t6_bcast_rows_complete = true;
    bool t6_bcast_rows_empty = true;

    for (const auto& row : get_t6_y_locations()) {
        t6_bcast_rows_complete &= (rows_to_exclude.find(row) == rows_to_exclude.end());
        t6_bcast_rows_empty &= (rows_to_exclude.find(row) != rows_to_exclude.end());
    }
    return t6_bcast_rows_complete || t6_bcast_rows_empty;
}


void TTDevice::bar_write32(uint32_t addr, uint32_t data) {
    // TTDevice* dev = ->hdev;

    if (addr < pci_device->bar0_uc_offset) {
        write_block(addr, sizeof(data), reinterpret_cast<const uint8_t*>(&data));
    } else {
        pci_device->write_regs(addr, 1, &data);
    }
}

uint32_t TTDevice::bar_read32(uint32_t addr) {
    uint32_t data;
    if (addr < pci_device->bar0_uc_offset) {
        read_block(addr, sizeof(data), reinterpret_cast<uint8_t*>(&data));
    } else {
        pci_device->read_regs(addr, 1, &data);
    }
    return data;
}


// Returns 0 if everything was OK
int TTDevice::pcie_arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    int timeout,
    uint32_t* return_3,
    uint32_t* return_4) {
    if ((msg_code & 0xff00) != 0xaa00) {
        ERROR("Malformed message. msg_code is 0x%x but should be 0xaa..\n", msg_code);
    }
    log_assert(arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args");  // Only 16 bits are allowed

    // auto architecture_implementation = pci_device->hdev->get_architecture_implementation();

    uint32_t fw_arg = arg0 | (arg1 << 16);
    int exit_code = 0;

    bar_write32(get_arc_reset_scratch_offset() + 3 * 4, fw_arg);
    bar_write32(get_arc_reset_scratch_offset() + 5 * 4, msg_code);

    uint32_t misc = bar_read32(get_arc_reset_arc_misc_cntl_offset());
    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", device_id);
        return 1;
    } else {
        bar_write32(get_arc_reset_arc_misc_cntl_offset(), misc | (1 << 16));
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto timeout_seconds = std::chrono::seconds(timeout);
        auto start = std::chrono::system_clock::now();
        while (true) {
            if (std::chrono::system_clock::now() - start > timeout_seconds) {
                throw std::runtime_error(
                    "Timed out after waiting " + std::to_string(timeout) + " seconds for device " +
                    std::to_string(device_id) + " ARC to respond");
            }

            status = bar_read32(get_arc_reset_scratch_offset() + 5 * 4);

            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    *return_3 = bar_read32(get_arc_reset_scratch_offset() + 3 * 4);
                }

                if (return_4 != nullptr) {
                    *return_4 = bar_read32(get_arc_reset_scratch_offset() + 4 * 4);
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == MSG_ERROR_REPLY) {
                log_warning(
                    LogSiliconDriver,
                    "On device {}, message code 0x{:x} not recognized by FW",
                    device_id,
                    msg_code);
                exit_code = MSG_ERROR_REPLY;
                break;
            }
        }
    }

    detect_ffffffff_read();
    return exit_code;
}

void TTDevice::disable_atu() {}

}  // namespace tt::umd