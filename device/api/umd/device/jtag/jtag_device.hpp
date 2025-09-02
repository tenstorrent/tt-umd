// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "umd/device/jtag/jtag.h"
#include "umd/device/types/arch.h"

class JtagDevice {
public:
    ~JtagDevice();

    static std::shared_ptr<JtagDevice> create(std::filesystem::path& binary_directory = jtag_library_path);

    uint32_t get_device_cnt() const;
    std::optional<uint32_t> get_efuse_harvesting(uint8_t chip_id) const;

    tt::ARCH get_jtag_arch(uint8_t chip_id);

    int open_jlink_by_serial_wrapper(uint8_t chip_id, unsigned int serial_number);
    int open_jlink_wrapper(uint8_t chip_id);
    std::optional<uint32_t> read_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset);
    std::optional<uint32_t> readmon_tdr(uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset);
    std::optional<int> writemon_tdr(
        uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset, uint32_t data);
    std::optional<int> write_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset, uint32_t data);
    void dbus_memdump(
        uint8_t chip_id,
        const char* client_name,
        const char* mem,
        const char* thread_id_name,
        const char* start_addr,
        const char* end_addr);
    std::optional<int> dbus_sigdump(
        uint8_t chip_id,
        const char* client_name,
        uint32_t dbg_client_id,
        uint32_t dbg_signal_sel_start,
        uint32_t dbg_signal_sel_end);
    void write32_axi(uint8_t chip_id, uint32_t address, uint32_t data);
    void write32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data);
    void write(uint8_t chip_id, const void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size);

    std::optional<uint32_t> read32_axi(uint8_t chip_id, uint32_t address);
    std::optional<uint32_t> read32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address);
    void read(uint8_t chip_id, void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size);

    std::optional<std::vector<uint32_t>> enumerate_jlink();
    void close_jlink(uint8_t chip_id);
    std::optional<uint32_t> read_id_raw(uint8_t chip_id);
    std::optional<uint32_t> read_id(uint8_t chip_id);
    std::optional<uint8_t> get_current_device_idx() const;
    int get_device_id(uint8_t chip_id) const;

    static std::filesystem::path jtag_library_path;

private:
    JtagDevice();
    void select_device(uint8_t chip_id);
    static std::unique_ptr<Jtag> jtag;
    std::vector<uint32_t> jlink_devices;
    std::vector<uint32_t> efuse_harvesting;
    std::optional<uint8_t> curr_device_idx = std::nullopt;
};
