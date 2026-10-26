// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/jtag/jtag_device.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tt-umd/jtag/jtag.hpp"
#include "tt-umd/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

namespace {

// The serial is used as the device id across UMD, which is an int.
bool fits_device_id(uint32_t jlink_serial) {
    return jlink_serial <= static_cast<uint32_t>(std::numeric_limits<int>::max());
}

}  // namespace

// Bare soname (no slash), so dlopen resolves it through the RPATH/RUNPATH and the standard loader search paths.
/* static */ std::filesystem::path JtagDevice::jtag_library_path = std::filesystem::path("libtt_umd_jtag.so");
/* static */ std::optional<uint32_t> JtagDevice::open_serial = std::nullopt;

JtagDevice::JtagDevice(std::unique_ptr<Jtag> jtag_library, uint32_t serial) :
    jtag(std::move(jtag_library)), serial(serial) {}

/* static */ std::unique_ptr<Jtag> JtagDevice::load_jtag_library(const std::filesystem::path& binary_directory) {
    std::filesystem::path actual_path = binary_directory;

    if (actual_path.empty()) {
        char buffer[PATH_MAX + 1];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

        if (len != -1) {
            buffer[len] = '\0';
            std::string path(buffer);
            std::string::size_type pos = path.find_last_of('/');
            actual_path = path.substr(0, pos);
        }
    }

    return std::make_unique<Jtag>(actual_path.c_str());
}

/* static */ JtagDevices JtagDevice::enumerate(
    const std::filesystem::path& binary_directory, const std::unordered_set<int>& jtag_target_devices) {
    std::unique_ptr<Jtag> jtag = load_jtag_library(binary_directory);

    // Probing reopens devices one by one, so whatever another instance had open is closed along the way.
    jtag->close_jlink();
    open_serial = std::nullopt;

    std::vector<uint32_t> usable_serials;
    std::optional<tt::ARCH> arch;
    for (uint32_t jlink_serial : get_jtag_visible_devices(*jtag, jtag_target_devices)) {
        if (!fits_device_id(jlink_serial)) {
            log_warning(tt::LogUMD, "Skipping J-Link {} whose serial does not fit into a device id.", jlink_serial);
            continue;
        }
        // Underlying JTAG library uses unix-style status returns. Success is represented by 0.
        if (jtag->open_jlink_by_serial_wrapper(jlink_serial) != 0) {
            log_debug(tt::LogUMD, "Skipping J-Link {} which failed to open.", jlink_serial);
            continue;
        }
        const tt::ARCH device_arch = device_family_to_arch.at((DeviceFamily)jtag->get_device_family());
        jtag->close_jlink();

        if (arch.has_value() && *arch != device_arch) {
            UMD_THROW(error::RuntimeError, "Jtag ERROR: Not all devices have the same architecture.");
        }
        arch = device_arch;
        usable_serials.push_back(jlink_serial);
    }

    if (usable_serials.empty()) {
        log_warning(tt::LogUMD, "There are no supported devices.");
    }
    return JtagDevices{usable_serials, arch.value_or(tt::ARCH::Invalid)};
}

/* static */ std::unique_ptr<JtagDevice> JtagDevice::create(
    uint32_t serial, const std::filesystem::path& binary_directory) {
    UMD_ASSERT(
        fits_device_id(serial),
        error::RuntimeError,
        fmt::format("J-Link serial {} does not fit into a device id.", serial));

    std::unique_ptr<Jtag> jtag = load_jtag_library(binary_directory);
    // Catches callers still passing a discovery index, which would otherwise fail only on first access.
    const std::vector<uint32_t> connected_serials = jtag->enumerate_jlink();
    if (std::find(connected_serials.begin(), connected_serials.end(), serial) == connected_serials.end()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "No J-Link with serial {} is connected. JTAG device ids are J-Link serials, connected J-Links: [{}].",
                serial,
                fmt::join(connected_serials, ", ")));
    }
    return std::make_unique<JtagDevice>(std::move(jtag), serial);
}

JtagDevice::~JtagDevice() {
    if (open_serial == serial) {
        jtag->close_jlink();
        open_serial = std::nullopt;
    }
}

uint32_t JtagDevice::get_serial() const { return serial; }

void JtagDevice::select_device() {
    if (open_serial == serial) {
        return;
    }

    jtag->close_jlink();
    open_serial = std::nullopt;

    // Underlying JTAG library uses unix-style status returns. Success is represented by 0.
    if (jtag->open_jlink_by_serial_wrapper(serial)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("JtagDevice::select_device: Failed to open J-Link with serial {}", serial));
    }
    open_serial = serial;
}

tt::ARCH JtagDevice::get_jtag_arch() {
    select_device();
    return device_family_to_arch.at((DeviceFamily)jtag->get_device_family());
}

// TODO: implement std::optional type return in JTAG library itself.
// For now JTAG library handles it by exiting the program on error.
std::optional<uint32_t> JtagDevice::read_tdr(const char* client, uint32_t reg_offset) {
    select_device();
    return jtag->read_tdr(client, reg_offset);
}

std::optional<uint32_t> JtagDevice::readmon_tdr(const char* client, uint32_t id, uint32_t reg_offset) {
    select_device();
    return jtag->readmon_tdr(client, id, reg_offset);
}

std::optional<int> JtagDevice::writemon_tdr(const char* client, uint32_t id, uint32_t reg_offset, uint32_t data) {
    select_device();
    jtag->writemon_tdr(client, id, reg_offset, data);
    return 0;
}

std::optional<int> JtagDevice::write_tdr(const char* client, uint32_t reg_offset, uint32_t data) {
    select_device();
    jtag->write_tdr(client, reg_offset, data);
    return 0;
}

void JtagDevice::dbus_memdump(
    const char* client_name,
    const char* mem,
    const char* thread_id_name,
    const char* start_addr,
    const char* end_addr) {
    select_device();
    jtag->dbus_memdump(client_name, mem, thread_id_name, start_addr, end_addr);
}

std::optional<int> JtagDevice::dbus_sigdump(
    const char* client_name, uint32_t dbg_client_id, uint32_t dbg_signal_sel_start, uint32_t dbg_signal_sel_end) {
    select_device();
    jtag->dbus_sigdump(client_name, dbg_client_id, dbg_signal_sel_start, dbg_signal_sel_end);
    return 0;
}

void JtagDevice::write32_axi(uint32_t address, uint32_t data) {
    select_device();
    jtag->write_axi(address, data);
}

void JtagDevice::write32(uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data, uint8_t noc_id) {
    select_device();
    jtag->write_noc_xy(noc_x, noc_y, address, data, noc_id);
}

void JtagDevice::write(
    const void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size, uint8_t noc_id) {
    const uint8_t* buffer_addr = static_cast<const uint8_t*>(mem_ptr);

    const uint32_t chunk_size = sizeof(uint32_t);

    while (size > 0) {
        uint32_t transfer_size = std::min(size, chunk_size);

        // JTAG protocol doesn't require address alignment to word size (4 bytes).
        if (transfer_size == sizeof(uint32_t)) {
            uint32_t data;
            std::memcpy(&data, buffer_addr, transfer_size);
            write32(noc_x, noc_y, addr, data, noc_id);

            size -= transfer_size;
            addr += transfer_size;
            buffer_addr += transfer_size;
            continue;
        }

        auto read_result = read32(noc_x, noc_y, addr, noc_id);
        if (!read_result) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "JTAG read32 failed for J-Link {} at core ({},{}) address 0x{:x}", serial, noc_x, noc_y, addr));
        }

        uint8_t* data_bytes = reinterpret_cast<uint8_t*>(&(*read_result));

        std::memcpy(data_bytes, buffer_addr, transfer_size);

        write32(noc_x, noc_y, addr, *read_result, noc_id);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

std::optional<uint32_t> JtagDevice::read32_axi(uint32_t address) {
    select_device();
    return jtag->read_axi(address);
}

std::optional<uint32_t> JtagDevice::read32(uint8_t noc_x, uint8_t noc_y, uint64_t address, uint8_t noc_id) {
    select_device();
    return jtag->read_noc_xy(noc_x, noc_y, address, noc_id);
}

void JtagDevice::read(void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size, uint8_t noc_id) {
    uint8_t* buffer_addr = static_cast<uint8_t*>(mem_ptr);

    const uint32_t chunk_size = sizeof(uint32_t);

    while (size > 0) {
        uint32_t transfer_size = std::min(size, chunk_size);

        // JTAG protocol doesn't require address alignment to word size (4 bytes).
        auto result = read32(noc_x, noc_y, addr, noc_id);
        if (!result) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "JTAG read32 failed for J-Link {} at core ({},{}) address 0x{:x}", serial, noc_x, noc_y, addr));
        }
        uint32_t data = *result;
        std::memcpy(buffer_addr, &data, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

std::optional<uint32_t> JtagDevice::read_id_raw() {
    select_device();
    return jtag->read_id_raw();
}

std::optional<uint32_t> JtagDevice::read_id() {
    select_device();
    return jtag->read_id();
}

/* static */ std::vector<uint32_t> JtagDevice::get_jtag_visible_devices(
    Jtag& jtag, const std::unordered_set<int>& jtag_target_devices) {
    std::vector<uint32_t> connected_devices = jtag.enumerate_jlink();
    if (connected_devices.empty()) {
        log_warning(tt::LogUMD, "There are no j-link devices connected to host.");
    }
    // The library gives no guarantee on enumeration order, sorting keeps the device order stable across runs.
    std::sort(connected_devices.begin(), connected_devices.end());

    const std::unordered_set<int> target_devices = tt::umd::utils::get_visible_devices(jtag_target_devices);
    if (target_devices.empty()) {
        return connected_devices;
    }

    for (int target_device : target_devices) {
        if (std::find(connected_devices.begin(), connected_devices.end(), static_cast<uint32_t>(target_device)) ==
            connected_devices.end()) {
            log_warning(tt::LogUMD, "Target JTAG device with id {} not connected", target_device);
        }
    }

    std::vector<uint32_t> visible_devices;
    std::copy_if(
        connected_devices.begin(),
        connected_devices.end(),
        std::back_inserter(visible_devices),
        [&](uint32_t jlink_serial) { return target_devices.count(static_cast<int>(jlink_serial)) > 0; });
    return visible_devices;
}

bool JtagDevice::is_hardware_hung() {
    // Timeout value 10 is chosen from JTAG library.
    // Checkout the library implementation for more details.
    uint32_t timeout = 10;
    std::optional<uint32_t> status;
    do {
        // Details about status format can be found inside JTAG library.
        status = read_tdr("arc", 0x4);
        status = status.value() >> 16;
        timeout--;
    } while (((status.value() & 0x1) == 0) && (timeout > 0));
    return timeout == 0;
}

}  // namespace tt::umd
