// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/types/telemetry.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cxxopts.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "common.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

using namespace tt::umd;

std::string run_default_telemetry(int pci_device, FirmwareInfoProvider* firmware_info_provider, tt::ARCH  /*arch*/) {
    if (firmware_info_provider == nullptr) {
        return fmt::format("Could not get information for device ID {}.", pci_device);
    }

    double asic_temperature = firmware_info_provider->get_asic_temperature();
    double board_temperature = firmware_info_provider->get_board_temperature().value_or(0);
    uint32_t aiclk = firmware_info_provider->get_aiclk().value_or(0);
    uint32_t axiclk = firmware_info_provider->get_axiclk().value_or(0);
    uint32_t arcclk = firmware_info_provider->get_arcclk().value_or(0);
    uint32_t fs = firmware_info_provider->get_fan_speed().value_or(0);
    uint32_t tdp = firmware_info_provider->get_tdp().value_or(0);
    uint32_t tdc = firmware_info_provider->get_tdc().value_or(0);
    uint32_t vcore = firmware_info_provider->get_vcore().value_or(0);

    return fmt::format(
        "Device ID {} - Chip {:.2f} °C, Board {:.2f} °C, AICLK {} MHz, AXICLK {} MHz, ARCCLK {} MHz, "
        "Fan {} rpm, TDP {} W, TDC {} A, VCORE {} mV",
        pci_device,
        asic_temperature,
        board_temperature,
        aiclk,
        axiclk,
        arcclk,
        fs,
        tdp,
        tdc,
        vcore);
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("telemetry", "Poll telemetry values from devices.");

    options.add_options()(
        "d,devices",
        "List of device pci ids to read telemetry for. If empty, will poll on all available devices",
        cxxopts::value<std::vector<std::string>>())(
        "t,tag",
        "Telemetry tag to read. If set to -1, will run default telemetry mode which works only for WH and reads aiclk, "
        "power, temperature and vcore. See device/api/umd/device/types/telemetry.hpp"
        "for all available tags.",
        cxxopts::value<int>()->default_value("-1"))(
        "f,freq", "Frequency of polling in microseconds.", cxxopts::value<int>()->default_value("1000"))(
        "o,outfile",
        "Output file to dump telemetry to. If omitted, will print out to stdout.",
        cxxopts::value<std::string>())("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    int frequency_us = result["freq"].as<int>();
    int telemetry_tag = result["tag"].as<int>();

    std::vector<int> discovered_pci_device_ids = PCIDevice::enumerate_devices();
    std::vector<int> pci_device_ids;

    if (result.count("devices")) {
        for (int device_id : extract_int_vector(result["devices"])) {
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

    std::ofstream output_file;
    if (result.count("outfile")) {
        output_file.open(result["outfile"].as<std::string>());
        if (!output_file.is_open()) {
            std::cerr << "Failed to open output file: " << result["outfile"].as<std::string>() << std::endl;
        }
    }

    std::vector<std::pair<int, std::unique_ptr<ArcTelemetryReader>>> telemetry_readers;
    std::vector<std::unique_ptr<TTDevice>> tt_devices;
    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

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
            auto firmware_info_provider = tt_devices.at(i)->get_firmware_info_provider();

            std::string telemetry_message;
            if (telemetry_tag == -1) {
                auto arch = tt_devices.at(i)->get_arch();
                if (arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::BLACKHOLE) {
                    telemetry_message = run_default_telemetry(device_id, firmware_info_provider, arch);
                } else {
                    throw std::runtime_error("Unsupported device architecture");
                }
            } else {
                uint32_t telemetry_value = telemetry_reader->read_entry(telemetry_tag);
                telemetry_message = fmt::format("Device id {} - Telemetry value: 0x{:x}", device_id, telemetry_value);
            }
            if (output_file.is_open()) {
                auto timestamp = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(timestamp);
                auto fractional_seconds =
                    std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()) % 1000000;

                std::stringstream ss;
                ss << std::put_time(std::localtime(&time), "%F %T") << "." << std::setfill('0') << std::setw(6)
                   << fractional_seconds.count() << " - " << telemetry_message;
                output_file << ss.str() << std::endl;
            } else {
                log_info(tt::LogUMD, "{}", telemetry_message);
            }
        }

        auto end_time = std::chrono::steady_clock::now();
        auto time_passed = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        uint64_t time_to_wait = frequency_us - time_passed;
        if (time_to_wait > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(time_to_wait));
        }
    }

    return 0;
}
