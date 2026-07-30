// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Bounded reproduction of the DRAM membar poll in LocalChip::set_membar_flag, used to diagnose the
// BH galaxy hang where initialize_membars never returns for the DRAM group (grp@0x0, 8 cores, 0xbb).
//
// This tool deliberately does NOT call Cluster::start_device(), so it never enters the unbounded poll
// in UMD. It replays the same write/poll sequence itself with a deadline and prints, for every DRAM
// channel/subchannel of every device: the value read before the write, the last value read while
// polling, how many reads it took, and the elapsed time. On timeout it re-issues the barrier write
// once and polls again, which separates a dead tile (never converges) from a dropped or clobbered
// write (converges after the rewrite).
//
// Exit code is 0 when every core echoed the barrier value, 1 otherwise.

#include <chrono>
#include <cstdint>
#include <cxxopts.hpp>
#include <iostream>
#include <string>
#include <vector>

#include "umd/device/cluster.hpp"
#include "umd/device/driver_atomics.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

struct ProbeResult {
    bool synced = false;
    bool synced_after_rewrite = false;
    bool scrub_failed = false;
    uint32_t value_before_write = 0;
    uint32_t last_readback = 0;
    uint64_t reads = 0;
    double elapsed_ms = 0.0;
    std::string error;
};

std::string hex(uint32_t value) { return fmt::format("0x{:08x}", value); }

// Write barrier_value to core:addr, then poll until it reads back or the deadline expires.
// Mirrors set_membar_flag for a single core, with a bound.
ProbeResult probe_core(
    Cluster& cluster,
    ChipId chip,
    const CoreCoord& core,
    uint64_t addr,
    uint32_t barrier_value,
    std::chrono::milliseconds timeout,
    bool rewrite_retry,
    bool scrub,
    uint32_t scrub_value) {
    ProbeResult result;

    auto write_word = [&](const uint32_t& value) {
        tt_driver_atomics::sfence();
        cluster.write_to_device(&value, sizeof(value), chip, core, addr);
        tt_driver_atomics::sfence();
    };

    auto write_barrier = [&]() { write_word(barrier_value); };

    auto poll = [&](std::chrono::steady_clock::time_point start) {
        while (std::chrono::steady_clock::now() - start < timeout) {
            uint32_t readback = 0;
            cluster.read_from_device(&readback, chip, core, addr, sizeof(readback));
            result.reads++;
            result.last_readback = readback;
            if (readback == barrier_value) {
                return true;
            }
        }
        return false;
    };

    const auto start = std::chrono::steady_clock::now();
    try {
        // A prior UMD init leaves 0xbb at this address, so without scrubbing the poll is satisfied by
        // the first read and the write/poll path is never exercised. Poison the word first, then read
        // it back: the readback is the discriminator (0xffffffff = tile not answering, scrub_value =
        // healthy, anything else = something else owns this word).
        if (scrub) {
            write_word(scrub_value);
        }
        cluster.read_from_device(&result.value_before_write, chip, core, addr, sizeof(result.value_before_write));
        result.scrub_failed = scrub && result.value_before_write != scrub_value;

        write_barrier();
        result.synced = poll(start);

        if (!result.synced && rewrite_retry) {
            write_barrier();
            result.synced_after_rewrite = poll(std::chrono::steady_clock::now());
            result.synced = result.synced_after_rewrite;
        }
    } catch (const std::exception& e) {
        // A read/write that overruns the per-op MMIO budget throws DeviceTimeoutError; keep sweeping
        // the remaining cores instead of aborting on the first bad one.
        result.error = e.what();
    }
    result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    cxxopts::Options options(
        "dram_membar_probe", "Bounded probe of the DRAM memory barrier poll (UMD set_membar_flag).");

    // clang-format off
    options.add_options()
        ("h,help", "Print usage")
        ("addr", "Barrier address (DRAM_BARRIER_BASE)", cxxopts::value<uint64_t>()->default_value("0"))
        ("value", "Barrier value written and polled for (MemBarFlag::RESET)", cxxopts::value<uint32_t>()->default_value("187"))
        ("timeout-ms", "Per-core poll deadline in milliseconds", cxxopts::value<uint64_t>()->default_value("1000"))
        ("iterations", "Number of full sweeps to run", cxxopts::value<uint32_t>()->default_value("1"))
        ("subchannel", "Probe only this subchannel (-1 = all)", cxxopts::value<int>()->default_value("-1"))
        ("no-rewrite-retry", "Do not re-issue the barrier write after a timeout")
        ("no-scrub", "Do not poison the barrier word before writing it (leaves a stale 0xbb able to satisfy the poll)")
        ("scrub-value", "Poison value written before the barrier value", cxxopts::value<uint32_t>()->default_value("3735928559"))
        ("keep-going", "Continue sweeping after the first failing core");
    // clang-format on

    auto args = options.parse(argc, argv);
    if (args.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    const uint64_t addr = args["addr"].as<uint64_t>();
    const uint32_t barrier_value = args["value"].as<uint32_t>();
    const auto timeout = std::chrono::milliseconds(args["timeout-ms"].as<uint64_t>());
    const uint32_t iterations = args["iterations"].as<uint32_t>();
    const int only_subchannel = args["subchannel"].as<int>();
    const bool rewrite_retry = args.count("no-rewrite-retry") == 0;
    const bool scrub = args.count("no-scrub") == 0;
    const uint32_t scrub_value = args["scrub-value"].as<uint32_t>();
    const bool keep_going = args.count("keep-going") != 0;

    Cluster cluster;
    const ClusterDescriptor* cluster_desc = cluster.get_cluster_description();
    std::vector<std::string> failures;

    for (uint32_t iteration = 0; iteration < iterations; iteration++) {
        std::cout << fmt::format("[dram-probe] iteration {} of {}", iteration + 1, iterations) << std::endl;

        for (ChipId chip : cluster.get_target_mmio_device_ids()) {
            const SocDescriptor& soc_desc = cluster.get_soc_descriptor(chip);
            const int num_channels = soc_desc.get_num_dram_channels();
            const auto dram_cores = soc_desc.get_dram_cores();
            const size_t num_subchannels = dram_cores.empty() ? 0 : dram_cores.front().size();

            std::cout << fmt::format(
                             "[dram-probe] chip {} pci {} asic_loc {} board 0x{:x} : {} dram channel(s) x {} "
                             "subchannel(s), dram_harvesting_mask 0x{:x}",
                             chip,
                             cluster.get_chip(chip)->get_tt_device()->get_pci_device()->get_device_num(),
                             (int)cluster_desc->get_asic_location(chip),
                             cluster_desc->get_board_id_for_chip(chip),
                             num_channels,
                             num_subchannels,
                             cluster_desc->get_harvesting_masks(chip).dram_harvesting_mask)
                      << std::endl;

            for (int channel = 0; channel < num_channels; channel++) {
                for (size_t subchannel = 0; subchannel < num_subchannels; subchannel++) {
                    if (only_subchannel >= 0 && (int)subchannel != only_subchannel) {
                        continue;
                    }

                    CoreCoord core;
                    try {
                        core = soc_desc.get_dram_core_for_channel(channel, subchannel, CoordSystem::TRANSLATED);
                    } catch (const std::exception& e) {
                        std::cout << fmt::format(
                                         "[dram-probe] chip {} ch {} sub {} : UNMAPPED ({})",
                                         chip,
                                         channel,
                                         subchannel,
                                         e.what())
                                  << std::endl;
                        continue;
                    }

                    const ProbeResult result = probe_core(
                        cluster, chip, core, addr, barrier_value, timeout, rewrite_retry, scrub, scrub_value);

                    const std::string line = fmt::format(
                        "[dram-probe] chip {} pci {} asic_loc {} ch {} sub {} core {} addr 0x{:x} : {} "
                        "expected {} before {} last {} reads {} elapsed {:.3f} ms{}{}{}",
                        chip,
                        cluster.get_chip(chip)->get_tt_device()->get_pci_device()->get_device_num(),
                        (int)cluster_desc->get_asic_location(chip),
                        channel,
                        subchannel,
                        core.str(),
                        addr,
                        result.error.empty() ? (result.synced ? "OK" : "TIMEOUT") : "ERROR",
                        hex(barrier_value),
                        hex(result.value_before_write),
                        hex(result.last_readback),
                        result.reads,
                        result.elapsed_ms,
                        result.synced_after_rewrite ? " (synced only after barrier write was re-issued)" : "",
                        result.scrub_failed ? " (scrub write did not land: poll was not a real test)" : "",
                        result.error.empty() ? "" : " error: " + result.error);
                    std::cout << line << std::endl;

                    if (!result.synced || !result.error.empty() || result.scrub_failed) {
                        failures.push_back(line);
                        if (!keep_going) {
                            std::cout
                                << fmt::format(
                                       "[dram-probe] stopping on first failure ({} failure(s)); pass --keep-going "
                                       "to sweep the rest",
                                       failures.size())
                                << std::endl;
                            return 1;
                        }
                    }
                }
            }
        }
    }

    std::cout << fmt::format("[dram-probe] done, {} failure(s)", failures.size()) << std::endl;
    for (const std::string& failure : failures) {
        std::cout << "[dram-probe] FAILED: " << failure << std::endl;
    }
    return failures.empty() ? 0 : 1;
}
