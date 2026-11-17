// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/jtag/jtag_device.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

// Empty: this build loads no JTAG library.
std::filesystem::path JtagDevice::jtag_library_path;

JtagDevice::JtagDevice(std::unique_ptr<Jtag> jtag_library, uint32_t serial) { UMD_NOT_SUPPORTED("JTAG"); }

JtagDevice::~JtagDevice() = default;

JtagDevices JtagDevice::enumerate(
    const std::filesystem::path& binary_directory, const std::unordered_set<int>& jtag_target_devices) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::unique_ptr<JtagDevice> JtagDevice::create(uint32_t serial, const std::filesystem::path& binary_directory) {
    UMD_NOT_SUPPORTED("JTAG");
}

uint32_t JtagDevice::get_serial() const { UMD_NOT_SUPPORTED("JTAG"); }

tt::ARCH JtagDevice::get_jtag_arch() { UMD_NOT_SUPPORTED("JTAG"); }

std::optional<uint32_t> JtagDevice::read_tdr(const char* client, uint32_t reg_offset) { UMD_NOT_SUPPORTED("JTAG"); }

std::optional<uint32_t> JtagDevice::readmon_tdr(const char* client, uint32_t id, uint32_t reg_offset) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::optional<int> JtagDevice::writemon_tdr(const char* client, uint32_t id, uint32_t reg_offset, uint32_t data) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::optional<int> JtagDevice::write_tdr(const char* client, uint32_t reg_offset, uint32_t data) {
    UMD_NOT_SUPPORTED("JTAG");
}

void JtagDevice::dbus_memdump(
    const char* client_name,
    const char* mem,
    const char* thread_id_name,
    const char* start_addr,
    const char* end_addr) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::optional<int> JtagDevice::dbus_sigdump(
    const char* client_name, uint32_t dbg_client_id, uint32_t dbg_signal_sel_start, uint32_t dbg_signal_sel_end) {
    UMD_NOT_SUPPORTED("JTAG");
}

void JtagDevice::write32_axi(uint32_t address, uint32_t data) { UMD_NOT_SUPPORTED("JTAG"); }

void JtagDevice::write32(uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data, uint8_t noc_id) {
    UMD_NOT_SUPPORTED("JTAG");
}

void JtagDevice::write(
    const void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size, uint8_t noc_id) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::optional<uint32_t> JtagDevice::read32_axi(uint32_t address) { UMD_NOT_SUPPORTED("JTAG"); }

std::optional<uint32_t> JtagDevice::read32(uint8_t noc_x, uint8_t noc_y, uint64_t address, uint8_t noc_id) {
    UMD_NOT_SUPPORTED("JTAG");
}

void JtagDevice::read(void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size, uint8_t noc_id) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::optional<uint32_t> JtagDevice::read_id_raw() { UMD_NOT_SUPPORTED("JTAG"); }

std::optional<uint32_t> JtagDevice::read_id() { UMD_NOT_SUPPORTED("JTAG"); }

bool JtagDevice::is_hardware_hung() { UMD_NOT_SUPPORTED("JTAG"); }

std::unique_ptr<Jtag> JtagDevice::load_jtag_library(const std::filesystem::path& binary_directory) {
    UMD_NOT_SUPPORTED("JTAG");
}

std::vector<uint32_t> JtagDevice::get_jtag_visible_devices(
    Jtag& jtag, const std::unordered_set<int>& jtag_target_devices) {
    UMD_NOT_SUPPORTED("JTAG");
}

void JtagDevice::select_device() { UMD_NOT_SUPPORTED("JTAG"); }

}  // namespace tt::umd
