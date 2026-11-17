// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/jtag/jtag.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "common/unsupported.hpp"

Jtag::Jtag(const char* lib_path) { UMD_NOT_SUPPORTED("JTAG"); }

int Jtag::open_jlink_by_serial_wrapper(unsigned int serial_number) { UMD_NOT_SUPPORTED("JTAG"); }

int Jtag::open_jlink_wrapper() { UMD_NOT_SUPPORTED("JTAG"); }

uint32_t Jtag::read_tdr(const char* client, uint32_t reg_offset) { UMD_NOT_SUPPORTED("JTAG"); }

uint32_t Jtag::readmon_tdr(const char* client, uint32_t id, uint32_t reg_offset) { UMD_NOT_SUPPORTED("JTAG"); }

void Jtag::writemon_tdr(const char* client, uint32_t id, uint32_t reg_offset, uint32_t data) {
    UMD_NOT_SUPPORTED("JTAG");
}

void Jtag::write_tdr(const char* client, uint32_t reg_offset, uint32_t data) { UMD_NOT_SUPPORTED("JTAG"); }

void Jtag::dbus_memdump(
    const char* client_name,
    const char* mem,
    const char* thread_id_name,
    const char* start_addr,
    const char* end_addr) {
    UMD_NOT_SUPPORTED("JTAG");
}

void Jtag::dbus_sigdump(
    const char* client_name, uint32_t dbg_client_id, uint32_t dbg_signal_sel_start, uint32_t dbg_signal_sel_end) {
    UMD_NOT_SUPPORTED("JTAG");
}

void Jtag::write_axi(uint32_t reg_addr, uint32_t data) { UMD_NOT_SUPPORTED("JTAG"); }

void Jtag::write_noc_xy(uint32_t node_x_id, uint32_t node_y_id, uint64_t noc_addr, uint32_t noc_data, uint8_t noc_id) {
    UMD_NOT_SUPPORTED("JTAG");
}

uint32_t Jtag::read_axi(uint32_t reg_addr) { UMD_NOT_SUPPORTED("JTAG"); }

uint32_t Jtag::read_noc_xy(uint32_t node_x_id, uint32_t node_y_id, uint64_t noc_addr, uint8_t noc_id) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::vector<uint32_t> Jtag::enumerate_jlink() { UMD_NOT_SUPPORTED("JTAG"); }

void Jtag::close_jlink() { UMD_NOT_SUPPORTED("JTAG"); }

uint32_t Jtag::read_id_raw() { UMD_NOT_SUPPORTED("JTAG"); }

uint32_t Jtag::read_id() { UMD_NOT_SUPPORTED("JTAG"); }

uint32_t Jtag::get_device_family() { UMD_NOT_SUPPORTED("JTAG"); }

void* Jtag::load_function(const char* name) { UMD_NOT_SUPPORTED("JTAG"); }

void Jtag::openLibrary(const std::string& filePath, int flags) { UMD_NOT_SUPPORTED("JTAG"); }
