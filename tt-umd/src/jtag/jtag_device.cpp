// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// JTAG is not supported by this build: no J-Link is ever usable, so enumeration finds none and every device operation
// throws.

#include "tt-umd/jtag/jtag_device.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_jtag_device_not_supported() { UMD_THROW(error::RuntimeError, "JTAG is not supported."); }

}  // namespace

std::filesystem::path JtagDevice::jtag_library_path = "libjtag.so";

std::optional<uint32_t> JtagDevice::open_serial;

JtagDevice::JtagDevice(std::unique_ptr<Jtag>, uint32_t) { throw_jtag_device_not_supported(); }

JtagDevice::~JtagDevice() = default;

JtagDevices JtagDevice::enumerate(const std::filesystem::path&, const std::unordered_set<int>&) {
    return JtagDevices{{}, tt::ARCH::Invalid};
}

std::unique_ptr<JtagDevice> JtagDevice::create(uint32_t, const std::filesystem::path&) {
    throw_jtag_device_not_supported();
}

uint32_t JtagDevice::get_serial() const { throw_jtag_device_not_supported(); }

tt::ARCH JtagDevice::get_jtag_arch() { throw_jtag_device_not_supported(); }

std::optional<uint32_t> JtagDevice::read_tdr(const char*, uint32_t) { throw_jtag_device_not_supported(); }

std::optional<uint32_t> JtagDevice::readmon_tdr(const char*, uint32_t, uint32_t) { throw_jtag_device_not_supported(); }

std::optional<int> JtagDevice::writemon_tdr(const char*, uint32_t, uint32_t, uint32_t) {
    throw_jtag_device_not_supported();
}

std::optional<int> JtagDevice::write_tdr(const char*, uint32_t, uint32_t) { throw_jtag_device_not_supported(); }

void JtagDevice::dbus_memdump(const char*, const char*, const char*, const char*, const char*) {
    throw_jtag_device_not_supported();
}

std::optional<int> JtagDevice::dbus_sigdump(const char*, uint32_t, uint32_t, uint32_t) {
    throw_jtag_device_not_supported();
}

void JtagDevice::write32_axi(uint32_t, uint32_t) { throw_jtag_device_not_supported(); }

void JtagDevice::write32(uint8_t, uint8_t, uint64_t, uint32_t, uint8_t) { throw_jtag_device_not_supported(); }

void JtagDevice::write(const void*, uint8_t, uint8_t, uint64_t, uint32_t, uint8_t) {
    throw_jtag_device_not_supported();
}

std::optional<uint32_t> JtagDevice::read32_axi(uint32_t) { throw_jtag_device_not_supported(); }

std::optional<uint32_t> JtagDevice::read32(uint8_t, uint8_t, uint64_t, uint8_t) { throw_jtag_device_not_supported(); }

void JtagDevice::read(void*, uint8_t, uint8_t, uint64_t, uint32_t, uint8_t) { throw_jtag_device_not_supported(); }

std::optional<uint32_t> JtagDevice::read_id_raw() { throw_jtag_device_not_supported(); }

std::optional<uint32_t> JtagDevice::read_id() { throw_jtag_device_not_supported(); }

bool JtagDevice::is_hardware_hung() { throw_jtag_device_not_supported(); }

std::unique_ptr<Jtag> JtagDevice::load_jtag_library(const std::filesystem::path&) { throw_jtag_device_not_supported(); }

std::vector<uint32_t> JtagDevice::get_jtag_visible_devices(Jtag&, const std::unordered_set<int>&) {
    throw_jtag_device_not_supported();
}

void JtagDevice::select_device() { throw_jtag_device_not_supported(); }

}  // namespace tt::umd
