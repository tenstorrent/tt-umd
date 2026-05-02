// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>

#include <array>
#include <cstdint>
#include <cxxopts.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt::umd;

static tt_xy_pair parse_core(const std::string& s) {
    auto comma = s.find(',');
    if (comma == std::string::npos) {
        throw std::invalid_argument(fmt::format("Invalid core '{}', expected x,y", s));
    }
    size_t x = std::stoul(s.substr(0, comma));
    size_t y = std::stoul(s.substr(comma + 1));
    return {x, y};
}

// Returns true if the token is a multicast range (contains '-').
static bool is_multicast_token(const std::string& s) { return s.find('-') != std::string::npos; }

// Parses "x1,y1-x2,y2" into a pair of cores.
static std::pair<tt_xy_pair, tt_xy_pair> parse_multicast(const std::string& s) {
    auto dash = s.find('-');
    return {parse_core(s.substr(0, dash)), parse_core(s.substr(dash + 1))};
}

int main(int argc, char* argv[]) {
    cxxopts::Options options(
        "test_io",
        "Execute a sequence of reads and multicast writes.\n"
        "  x,y         read 16 bytes from core (x,y)\n"
        "  x1,y1-x2,y2 multicast-write 16 zero bytes to the core range");

    options.add_options()("h,help", "Print usage")(
        "ops",
        "Operations: x,y for read or x1,y1-x2,y2 for multicast write",
        cxxopts::value<std::vector<std::string>>());

    options.parse_positional({"ops"});
    options.positional_help("op [op ...]");

    // Pre-parse --read_after_multicast before cxxopts sees argv: cxxopts splits
    // on ',' for vector<string>, which conflicts with the x,y coordinate format.
    std::vector<tt_xy_pair> read_after_multicast_cores;
    std::vector<const char*> filtered_argv;
    filtered_argv.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--read_after_multicast") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
                read_after_multicast_cores.push_back(parse_core(argv[i]));
            }
        } else {
            filtered_argv.push_back(argv[i]);
        }
    }
    int filtered_argc = static_cast<int>(filtered_argv.size());
    auto result = options.parse(filtered_argc, filtered_argv.data());

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (!result.count("ops")) {
        std::cerr << "Usage: test_io op [op ...]\n"
                     "  op: x,y to read, or x1,y1-x2,y2 to multicast write\n";
        return 1;
    }

    const auto& ops = result["ops"].as<std::vector<std::string>>();

    auto devices = PCIDevice::enumerate_devices();
    if (devices.empty()) {
        log_error(tt::LogUMD, "No PCIe devices found.");
        return 1;
    }

    auto tt_device = TTDevice::create(devices[0]);
    tt_device->init_tt_device();

    {
        const tt::ChipInfo chip_info = tt_device->get_chip_info();
        const size_t mask = chip_info.harvesting_masks.tensix_harvesting_mask;
        const bool is_blackhole = tt_device->get_arch() == tt::ARCH::BLACKHOLE;
        const char* index_label = is_blackhole ? "column" : "row";

        std::vector<size_t> harvested;
        for (size_t i = 0; i < 32; ++i) {
            if (mask & (size_t(1) << i)) {
                harvested.push_back(i);
            }
        }

        std::string harvested_str;
        for (size_t i = 0; i < harvested.size(); ++i) {
            if (i > 0) {
                harvested_str += ", ";
            }
            harvested_str += std::to_string(harvested[i]);
        }

        log_info(
            tt::LogUMD,
            "Tensix harvesting mask: 0x{:x} | harvested {}s: [{}]",
            mask,
            index_label,
            harvested.empty() ? "none" : harvested_str);
    }

    static constexpr size_t kBytes = 16;
    static constexpr uint64_t kAddr = 0x100;

    uint8_t multicast_value = 0;

    for (const auto& op : ops) {
        if (is_multicast_token(op)) {
            auto [core_start, core_end] = parse_multicast(op);
            std::array<uint8_t, kBytes> fill{};
            fill.fill(multicast_value);
            tt_device->noc_multicast_write(fill.data(), kBytes, core_start, core_end, kAddr);
            log_info(
                tt::LogUMD,
                "Multicast-wrote {} bytes (value=0x{:02x}) to cores ({},{})..({},{}) at addr 0x{:x}",
                kBytes,
                multicast_value,
                core_start.x,
                core_start.y,
                core_end.x,
                core_end.y,
                kAddr);
            ++multicast_value;

            for (const auto& core : read_after_multicast_cores) {
                std::array<uint8_t, kBytes> buf{};
                tt_device->read_from_device(buf.data(), core, kAddr, kBytes);
                std::string hex;
                for (size_t i = 0; i < kBytes; i++) {
                    if (i > 0) {
                        hex += ' ';
                    }
                    hex += fmt::format("0x{:02x}", buf[i]);
                }
                log_info(
                    tt::LogUMD,
                    "Read {} bytes from core ({},{}) at addr 0x{:x}: [{}]",
                    kBytes,
                    core.x,
                    core.y,
                    kAddr,
                    hex);
            }
        } else {
            tt_xy_pair core = parse_core(op);
            std::array<uint8_t, kBytes> buf{};
            tt_device->read_from_device(buf.data(), core, kAddr, kBytes);
            std::string hex;
            for (size_t i = 0; i < kBytes; i++) {
                if (i > 0) {
                    hex += ' ';
                }
                hex += fmt::format("0x{:02x}", buf[i]);
            }
            log_info(
                tt::LogUMD, "Read {} bytes from core ({},{}) at addr 0x{:x}: [{}]", kBytes, core.x, core.y, kAddr, hex);
        }
    }

    return 0;
}
