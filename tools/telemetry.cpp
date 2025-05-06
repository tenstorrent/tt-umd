// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "logger.hpp"
#include "umd/device/arc_telemetry_reader.h"
#include "umd/device/types/wormhole_telemetry.h"

using namespace tt::umd;

void run_default_telemetry(int pci_device, ArcTelemetryReader* telemetry_reader) {
    uint32_t aiclk_fmax = telemetry_reader->read_entry(wormhole::TAG_AICLK);
    uint32_t aiclk_current = aiclk_fmax & 0xFFFF;
    // uint32_t aiclk_fmax = aiclk_fmax >> 16;
    uint32_t vcore = telemetry_reader->read_entry(wormhole::TAG_VCORE);
    uint32_t tdp = telemetry_reader->read_entry(wormhole::TAG_TDP) & 0xFFFF;
    uint32_t asic_temperature = telemetry_reader->read_entry(wormhole::TAG_ASIC_TEMPERATURE);
    uint32_t current_temperature = (asic_temperature & 0xFFFF) / 16.0;

    log_info(
        tt::LogSiliconDriver,
        "Device id {} - AICLK: {} VCore: {} TDP: {} Temp: {}",
        pci_device,
        aiclk_current,
        vcore,
        tdp,
        current_temperature);
}

std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delim)) {
        tokens.push_back(token);
    }

    return tokens;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "help") {
        std::cerr << "Usage: telemetry <telemetry_tag> <frequency_ms> <device_ids>" << std::endl;
        std::cerr << "Example: telemetry 0,1,2,3 4 2" << std::endl;
        std::cerr << "First argument is the list of device ids. For example 0,1,2,3. This refer to pci_ids"
                  << std::endl;
        std::cerr << "Second argument is the telemetry tag value. See all values in "
                     "device/api/umd/device/types/wormhole_telemetry.h or blackhole_telemetry.h"
                  << std::endl;
        std::cerr << "Third argument is the frequency of pooling the telemetry in milliseconds" << std::endl;
        return 1;
    }

    int frequency_ms = 1;
    if (argc >= 4) {
        frequency_ms = std::stoi(argv[3]);
    }
    int telemetry_tag = -1;
    if (argc >= 3) {
        telemetry_tag = std::stoi(argv[2]);
    }

    std::vector<int> discovered_pci_device_ids = PCIDevice::enumerate_devices();
    std::vector<int> pci_device_ids;
    if (argc >= 2) {
        auto device_ids_str = split(argv[1], ',');

        for (auto& device_id_str : device_ids_str) {
            int device_id = std::stoi(device_id_str);
            if (std::find(discovered_pci_device_ids.begin(), discovered_pci_device_ids.end(), device_id) ==
                discovered_pci_device_ids.end()) {
                std::cerr << "Device ID with pci id " << device_id << " not found in the system." << std::endl;
                // Ignore this device id and continue.
            } else {
                pci_device_ids.push_back(device_id);
            }
        }
    } else {
        pci_device_ids = discovered_pci_device_ids;
    }

    std::vector<std::pair<int, std::unique_ptr<ArcTelemetryReader>>> telemetry_readers;
    std::vector<std::unique_ptr<TTDevice>> tt_devices;
    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        std::unique_ptr<ArcTelemetryReader> arc_telemetry_reader =
            ArcTelemetryReader::create_arc_telemetry_reader(tt_device.get());
        tt_devices.push_back(std::move(tt_device));
        telemetry_readers.push_back(std::make_pair(pci_device_id, std::move(arc_telemetry_reader)));
    }

    while (true) {
        auto start_time = std::chrono::steady_clock::now();
        for (int i = 0; i < telemetry_readers.size(); i++) {
            int device_id = telemetry_readers.at(i).first;
            auto& telemetry_reader = telemetry_readers.at(i).second;

            if (telemetry_tag == -1) {
                run_default_telemetry(device_id, telemetry_reader.get());
            } else {
                uint32_t telemetry_value = telemetry_reader->read_entry(telemetry_tag);
                log_info(tt::LogSiliconDriver, "Device id {} - Telemetry value: 0x{:x}", device_id, telemetry_value);
            }
        }

        auto end_time = std::chrono::steady_clock::now();
        auto time_passed = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        uint64_t time_to_wait = frequency_ms * 1000 - time_passed;
        if (time_to_wait > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(time_to_wait));
        }
    }

    return 0;
}
