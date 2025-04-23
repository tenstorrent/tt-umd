/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/remote_chip.h"

#include "logger.hpp"
#include "umd/device/chip/local_chip.h"

extern bool umd_use_noc1;

namespace tt::umd {

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor, eth_coord_t eth_chip_location, LocalChip* local_chip) :
    Chip(soc_descriptor),
    eth_chip_location_(eth_chip_location),
    remote_communication_(std::make_unique<RemoteCommunication>(local_chip)) {
    log_assert(soc_descriptor_.arch != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");
}

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor, ChipInfo chip_info) : Chip(chip_info, soc_descriptor) {}

bool RemoteChip::is_mmio_capable() const { return false; }

void RemoteChip::start_device() {}

void RemoteChip::write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) {
    auto translated_core = translate_chip_coord_virtual_to_translated(core);
    remote_communication_->write_to_non_mmio(eth_chip_location_, translated_core, src, l1_dest, size);
}

void RemoteChip::read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) {
    auto translated_core = translate_chip_coord_virtual_to_translated(core);
    remote_communication_->read_non_mmio(eth_chip_location_, translated_core, dest, l1_src, size);
}

void RemoteChip::write_to_device_reg(tt_xy_pair core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void RemoteChip::read_from_device_reg(tt_xy_pair core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

// TODO: This translation should go away when we start using CoreCoord everywhere.
tt_xy_pair RemoteChip::translate_chip_coord_virtual_to_translated(const tt_xy_pair core) {
    CoreCoord core_coord = get_soc_descriptor().get_coord_at(core, CoordSystem::VIRTUAL);
    // Since NOC1 and translated coordinate space overlaps for Tensix cores on Blackhole,
    // Tensix cores are always used in translated space. Other cores are used either in
    // NOC1 or translated space depending on the umd_use_noc1 flag.
    // On Wormhole Tensix can use NOC1 space if umd_use_noc1 is set to true.
    if (get_soc_descriptor().noc_translation_enabled) {
        if (get_soc_descriptor().arch == tt::ARCH::BLACKHOLE) {
            if (core_coord.core_type == CoreType::TENSIX || !umd_use_noc1) {
                return get_soc_descriptor().translate_coord_to(core_coord, CoordSystem::TRANSLATED);
            } else {
                return get_soc_descriptor().translate_coord_to(core_coord, CoordSystem::NOC1);
            }
        } else {
            return get_soc_descriptor().translate_coord_to(
                core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
        }
    } else {
        return get_soc_descriptor().translate_coord_to(
            core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
    }
}

void RemoteChip::wait_for_non_mmio_flush() {
    log_assert(soc_descriptor_.arch != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");
    remote_communication_->wait_for_non_mmio_flush();
}

int RemoteChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    constexpr uint64_t ARC_RESET_SCRATCH_ADDR = 0x880030060;
    constexpr uint64_t ARC_RESET_MISC_CNTL_ADDR = 0x880030100;

    auto arc_core = soc_descriptor_.get_cores(CoreType::ARC).at(0);

    if ((msg_code & 0xff00) != 0xaa00) {
        log_error("Malformed message. msg_code is 0x{:x} but should be 0xaa..", msg_code);
    }
    log_assert(arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args");  // Only 16 bits are allowed

    uint32_t fw_arg = arg0 | (arg1 << 16);
    int exit_code = 0;

    { write_to_device(arc_core, &fw_arg, ARC_RESET_SCRATCH_ADDR + 3 * 4, sizeof(fw_arg)); }

    { write_to_device(arc_core, &msg_code, ARC_RESET_SCRATCH_ADDR + 5 * 4, sizeof(fw_arg)); }

    wait_for_non_mmio_flush();
    uint32_t misc = 0;

    read_from_device(arc_core, &misc, ARC_RESET_MISC_CNTL_ADDR, 4);

    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device");
        return 1;
    } else {
        misc |= (1 << 16);
        write_to_device(arc_core, &misc, ARC_RESET_MISC_CNTL_ADDR, sizeof(misc));
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed_ms > timeout_ms && timeout_ms != 0) {
                std::stringstream ss;
                ss << std::hex << msg_code;
                throw std::runtime_error(fmt::format(
                    "Timed out after waiting {} ms for device ARC to respond to message 0x{}", timeout_ms, ss.str()));
            }

            uint32_t status = 0;
            read_from_device(arc_core, &status, ARC_RESET_SCRATCH_ADDR + 5 * 4, sizeof(status));
            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    read_from_device(arc_core, return_3, ARC_RESET_SCRATCH_ADDR + 3 * 4, sizeof(uint32_t));
                }

                if (return_4 != nullptr) {
                    read_from_device(arc_core, return_4, ARC_RESET_SCRATCH_ADDR + 4 * 4, sizeof(uint32_t));
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == HANG_READ_VALUE) {
                log_warning(LogSiliconDriver, "Message code 0x{:x} not recognized by FW", msg_code);
                exit_code = HANG_READ_VALUE;
                break;
            }
        }
    }
    return exit_code;
}

void RemoteChip::l1_membar(const std::unordered_set<tt::umd::CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<tt::umd::CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<uint32_t>& channels) { wait_for_non_mmio_flush(); }

}  // namespace tt::umd
