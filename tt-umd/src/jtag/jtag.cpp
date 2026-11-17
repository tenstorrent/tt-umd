// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// JTAG is not supported by this build: the J-Link library is never loaded, so constructing a Jtag fails the way a
// missing library does and callers that probe for it treat JTAG as unavailable.

#include "tt-umd/jtag/jtag.hpp"

#include <dlfcn.h>

#include <string>

#include "tt-umd/utils/error.hpp"

namespace {

[[noreturn]] void throw_jtag_not_supported() { UMD_THROW(tt::umd::error::RuntimeError, "JTAG is not supported."); }

}  // namespace

void DlCloser::operator()(void* handle) const {
    if (handle != nullptr) {
        dlclose(handle);
    }
}

DlHandle Jtag::handle;

Jtag::Jtag(const char* lib_path) {
    UMD_THROW(
        tt::umd::error::RuntimeError, std::string("JTAG is not supported; cannot load the JTAG library ") + lib_path);
}

int Jtag::open_jlink_by_serial_wrapper(unsigned int) { throw_jtag_not_supported(); }

int Jtag::open_jlink_wrapper() { throw_jtag_not_supported(); }

uint32_t Jtag::read_tdr(const char*, uint32_t) { throw_jtag_not_supported(); }

uint32_t Jtag::readmon_tdr(const char*, uint32_t, uint32_t) { throw_jtag_not_supported(); }

void Jtag::writemon_tdr(const char*, uint32_t, uint32_t, uint32_t) { throw_jtag_not_supported(); }

void Jtag::write_tdr(const char*, uint32_t, uint32_t) { throw_jtag_not_supported(); }

void Jtag::dbus_memdump(const char*, const char*, const char*, const char*, const char*) { throw_jtag_not_supported(); }

void Jtag::dbus_sigdump(const char*, uint32_t, uint32_t, uint32_t) { throw_jtag_not_supported(); }

void Jtag::write_axi(uint32_t, uint32_t) { throw_jtag_not_supported(); }

void Jtag::write_noc_xy(uint32_t, uint32_t, uint64_t, uint32_t, uint8_t) { throw_jtag_not_supported(); }

uint32_t Jtag::read_axi(uint32_t) { throw_jtag_not_supported(); }

uint32_t Jtag::read_noc_xy(uint32_t, uint32_t, uint64_t, uint8_t) { throw_jtag_not_supported(); }

std::vector<uint32_t> Jtag::enumerate_jlink() { throw_jtag_not_supported(); }

void Jtag::close_jlink() { throw_jtag_not_supported(); }

uint32_t Jtag::read_id_raw() { throw_jtag_not_supported(); }

uint32_t Jtag::read_id() { throw_jtag_not_supported(); }

uint32_t Jtag::get_device_family() { throw_jtag_not_supported(); }

void* Jtag::load_function(const char*) { throw_jtag_not_supported(); }

void Jtag::openLibrary(const std::string&, int) { throw_jtag_not_supported(); }
