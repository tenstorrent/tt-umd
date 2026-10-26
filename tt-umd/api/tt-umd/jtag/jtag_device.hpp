// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tt-umd/jtag/jtag.hpp"
#include "tt-umd/types/arch.hpp"

class Jtag;

typedef enum { DEVICE_FAMILY_UNKNOWN, DEVICE_FAMILY_WORMHOLE, DEVICE_FAMILY_BLACKHOLE } DeviceFamily;

inline const std::unordered_map<DeviceFamily, tt::ARCH> device_family_to_arch = {
    {DeviceFamily::DEVICE_FAMILY_WORMHOLE, tt::ARCH::WORMHOLE_B0},
    {DeviceFamily::DEVICE_FAMILY_BLACKHOLE, tt::ARCH::BLACKHOLE},
    {DeviceFamily::DEVICE_FAMILY_UNKNOWN, tt::ARCH::Invalid}};

namespace tt::umd {

// J-Links found by JtagDevice::enumerate().
struct JtagDevices {
    std::vector<uint32_t> serials;  // Ascending.
    tt::ARCH arch;                  // Shared by all of them, Invalid when there are none.
};

// A single J-Link probe, identified by its serial number. The serial is what names the device everywhere in UMD,
// since it is the only identifier that stays the same across instances and processes.
class JtagDevice {
public:
    explicit JtagDevice(std::unique_ptr<Jtag> jtag_library, uint32_t serial);
    ~JtagDevice();

    // The connected J-Links that can be opened, have a serial usable as a device id and pass the target device filter
    // (TT_VISIBLE_DEVICES when jtag_target_devices is empty). Throws if their architectures differ.
    static JtagDevices enumerate(
        const std::filesystem::path& binary_directory = jtag_library_path,
        const std::unordered_set<int>& jtag_target_devices = {});

    // Throws if no J-Link with this serial is connected.
    static std::unique_ptr<JtagDevice> create(
        uint32_t serial, const std::filesystem::path& binary_directory = jtag_library_path);

    uint32_t get_serial() const;

    tt::ARCH get_jtag_arch();

    /*
     * client -> debug client name (e.g. "arc", "pcie"). Communicates with JTAG ports on clients through TDR(TAP Data
     * Register). reg_offset -> Register offset inside the client.
     */
    std::optional<uint32_t> read_tdr(const char* client, uint32_t reg_offset);
    std::optional<uint32_t> readmon_tdr(const char* client, uint32_t id, uint32_t reg_offset);
    std::optional<int> writemon_tdr(const char* client, uint32_t id, uint32_t reg_offset, uint32_t data);
    std::optional<int> write_tdr(const char* client, uint32_t reg_offset, uint32_t data);
    void dbus_memdump(
        const char* client_name,
        const char* mem,
        const char* thread_id_name,
        const char* start_addr,
        const char* end_addr);
    std::optional<int> dbus_sigdump(
        const char* client_name, uint32_t dbg_client_id, uint32_t dbg_signal_sel_start, uint32_t dbg_signal_sel_end);
    void write32_axi(uint32_t address, uint32_t data);
    void write32(uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data, uint8_t noc_id = 0);
    void write(const void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size, uint8_t noc_id = 0);

    std::optional<uint32_t> read32_axi(uint32_t address);
    std::optional<uint32_t> read32(uint8_t noc_x, uint8_t noc_y, uint64_t address, uint8_t noc_id = 0);
    void read(void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size, uint8_t noc_id = 0);

    std::optional<uint32_t> read_id_raw();
    std::optional<uint32_t> read_id();
    bool is_hardware_hung();
    static std::filesystem::path jtag_library_path;

private:
    static std::unique_ptr<Jtag> load_jtag_library(const std::filesystem::path& binary_directory);
    static std::vector<uint32_t> get_jtag_visible_devices(
        Jtag& jtag, const std::unordered_set<int>& jtag_target_devices);
    void select_device();

    std::unique_ptr<Jtag> jtag;
    uint32_t serial;
    // The J-Link library keeps a single open device for the whole process, shared by all instances.
    static std::optional<uint32_t> open_serial;
};

}  // namespace tt::umd
