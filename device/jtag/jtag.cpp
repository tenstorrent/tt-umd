// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/jtag/jtag.hpp"

#include <stdint.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>
#include <vector>

#include "assert.hpp"

/*static*/ DlHandle Jtag::handle;

void DlCloser::operator()(void* handle) const {
    if (handle != nullptr) {
        dlclose(handle);
        log_debug(tt::LogUMD, "JTAG library closed");
    }
}

void Jtag::openLibrary(const std::string& filePath, int flags) {
    if (!std::filesystem::exists(filePath)) {
        TT_THROW(
            "You do not have a JTAG library at {}.\n"
            "File path could be wrong.",
            filePath);
    }
    if (handle != nullptr) {
        return;
    }

    handle = DlHandle(dlopen(filePath.c_str(), flags));

    if (!handle) {
        TT_THROW("Failed to open JTAG library: {}", dlerror());
    }

    log_info(tt::LogUMD, "JTAG library {} opened successfully.", filePath);
}

Jtag::Jtag(const char* lib_path) { openLibrary(lib_path); }

void* Jtag::load_function(const char* name) {
    if (func_map.find(name) == func_map.end()) {
        void* funcPtr = dlsym(handle.get(), name);
        const char* dlsym_error = dlerror();
        if (dlsym_error) {
            std::cerr << "Cannot load symbol: " << dlsym_error << '\n';
            throw std::runtime_error("Failed to load function");
        }
        func_map[name] = funcPtr;
    }
    return func_map[name];
}

template <typename T>
struct RemoveThis;

template <typename ClassType, typename RetType, typename... ArgTypes>
struct RemoveThis<RetType (ClassType::*)(ArgTypes...)> {
    using type = RetType(ArgTypes...);
};

template <typename Method>
using decltypemagic = typename RemoveThis<Method>::type;

#define GET_FUNCTION_POINTER_MANGLED(name, mangled_name) \
    reinterpret_cast<decltypemagic<decltype(&Jtag::name)>*>(load_function(#mangled_name))

#define GET_FUNCTION_POINTER(name) GET_FUNCTION_POINTER_MANGLED(name, tt_##name)

int Jtag::open_jlink_by_serial_wrapper(unsigned int serial_number) {
    return GET_FUNCTION_POINTER(open_jlink_by_serial_wrapper)(serial_number);
}

int Jtag::open_jlink_wrapper() { return GET_FUNCTION_POINTER(open_jlink_wrapper)(); }

uint32_t Jtag::read_tdr(const char* client, uint32_t reg_offset) {
    return GET_FUNCTION_POINTER(read_tdr)(client, reg_offset);
}

uint32_t Jtag::readmon_tdr(const char* client, uint32_t id, uint32_t reg_offset) {
    return GET_FUNCTION_POINTER(readmon_tdr)(client, id, reg_offset);
}

void Jtag::writemon_tdr(const char* client, uint32_t id, uint32_t reg_offset, uint32_t data) {
    GET_FUNCTION_POINTER(writemon_tdr)(client, id, reg_offset, data);
}

void Jtag::write_tdr(const char* client, uint32_t reg_offset, uint32_t data) {
    GET_FUNCTION_POINTER(write_tdr)(client, reg_offset, data);
}

void Jtag::dbus_memdump(
    const char* client_name,
    const char* mem,
    const char* thread_id_name,
    const char* start_addr,
    const char* end_addr) {
    GET_FUNCTION_POINTER(dbus_memdump)(client_name, mem, thread_id_name, start_addr, end_addr);
}

void Jtag::dbus_sigdump(
    const char* client_name, uint32_t dbg_client_id, uint32_t dbg_signal_sel_start, uint32_t dbg_signal_sel_end) {
    GET_FUNCTION_POINTER(dbus_sigdump)(client_name, dbg_client_id, dbg_signal_sel_start, dbg_signal_sel_end);
}

void Jtag::write_axi(uint32_t reg_addr, uint32_t data) { GET_FUNCTION_POINTER(write_axi)(reg_addr, data); }

void Jtag::write_noc_xy(uint32_t node_x_id, uint32_t node_y_id, uint64_t noc_addr, uint32_t noc_data, uint8_t noc_id) {
    return GET_FUNCTION_POINTER(write_noc_xy)(node_x_id, node_y_id, noc_addr, noc_data, noc_id);
}

uint32_t Jtag::read_axi(uint32_t reg_addr) { return GET_FUNCTION_POINTER(read_axi)(reg_addr); }

uint32_t Jtag::read_noc_xy(uint32_t node_x_id, uint32_t node_y_id, uint64_t noc_addr, uint8_t noc_id) {
    return GET_FUNCTION_POINTER(read_noc_xy)(node_x_id, node_y_id, noc_addr, noc_id);
}

std::vector<uint32_t> Jtag::enumerate_jlink() {
    return GET_FUNCTION_POINTER_MANGLED(enumerate_jlink, _Z18tt_enumerate_jlinkv)();
}

void Jtag::close_jlink() { GET_FUNCTION_POINTER(close_jlink)(); }

uint32_t Jtag::read_id_raw() { return GET_FUNCTION_POINTER(read_id_raw)(); }

uint32_t Jtag::read_id() { return GET_FUNCTION_POINTER(read_id)(); }

uint32_t Jtag::get_device_family() { return GET_FUNCTION_POINTER(get_device_family)(); }
