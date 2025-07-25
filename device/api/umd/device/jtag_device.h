// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "lib/jtag/inc/jtag.h"

class JTAGDevice {
   public:
    explicit JTAGDevice(std::unique_ptr<Jtag> jtag_device);
    ~JTAGDevice();

    std::optional<uint32_t> get_device_cnt() const;
    std::optional<uint32_t> get_efuse_harvesting(uint8_t chip_id) const;

    std::optional<tt::ARCH> get_jtag_arch(uint8_t chip_id);

    std::optional<int> open_jlink_by_serial_wrapper(uint8_t chip_id, unsigned int serial_number);
    std::optional<int> open_jlink_wrapper(uint8_t chip_id);
    std::optional<uint32_t> read_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset);
    std::optional<uint32_t> readmon_tdr(uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset);
    std::optional<int> writemon_tdr(uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset,
                                    uint32_t data);
    std::optional<int> write_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset, uint32_t data);
    std::optional<int> dbus_memdump(uint8_t chip_id, const char* client_name, const char* mem,
                                    const char* thread_id_name, const char* start_addr, const char* end_addr);
    std::optional<int> dbus_sigdump(uint8_t chip_id, const char* client_name, uint32_t dbg_client_id,
                                    uint32_t dbg_signal_sel_start, uint32_t dbg_signal_sel_end);
    std::optional<int> write32_axi(uint8_t chip_id, uint32_t address, uint32_t data);
    std::optional<int> write32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data);
    std::optional<uint32_t> read32_axi(uint8_t chip_id, uint32_t address);
    std::optional<uint32_t> read32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address);
    std::optional<std::vector<uint32_t>> enumerate_jlink(uint8_t chip_id);
    std::optional<int> close_jlink(uint8_t chip_id);
    std::optional<uint32_t> read_id_raw(uint8_t chip_id);
    std::optional<uint32_t> read_id(uint8_t chip_id);

   private:
    bool select_device(uint8_t chip_id);
    std::unique_ptr<Jtag> jtag;
    std::vector<uint32_t> jlink_devices;
    std::vector<uint32_t> efuse_harvesting;
    uint8_t curr_device_idx = -1;
};