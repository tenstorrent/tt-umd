// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
//
// dram_pio_probe - is a given DRAM range actually backed and correctly addressed?
//
// No verbs, no dma-buf, no NIC, no P2P: this writes a position-dependent pattern over a NOC
// address range with Cluster::write_to_device() and reads it back with read_from_device(), then
// compares. It exists to answer one question that the RDMA tools cannot: when a P2P transfer to
// the top of a DRAM bank fails, is that because the DRAM is not there / not addressable, or
// because something in the dma-buf export path is wrong?
//
// Note that a bare PIO *write* proves nothing on its own - write_to_device() memcpys into a
// write-combining BAR mapping, so posted writes into a hole report the same bandwidth as writes
// into real memory. Only the read-back tells you anything, which is why this is two-phase.
//
// All writes complete before any read starts. That ordering is deliberate: if the top of the
// range aliases back onto the bottom, the tail writes clobber the head and the read phase reports
// a mismatch at the head. The pattern encodes each word's own absolute address, so a mismatch is
// decoded into "this is the value that belongs at <other address>" and aliasing is named outright
// rather than just showing up as garbage.
//
// A read to an unresponsive NOC address hangs rather than failing, and leaves the board needing a
// reset, so prefer small targeted ranges with -v (announce each chunk, so the last line printed
// names the offset that hung) and -1 (verify each chunk immediately instead of writing the whole
// range up front).
//
// To run (defaults to the whole 4 GiB of DRAM bank 0 on chip 0):
//   ./dram_pio_probe -v -1 -a 0 -s 64 -k 2               # low control, 2 MiB resolution
//   ./dram_pio_probe -v -1 -a 0xFC000000 -s 64 -k 2      # the top 64 MiB
//   ./dram_pio_probe -b 3 -s 4096                        # a different bank, full two-phase

#include <unistd.h>

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

static constexpr size_t MIB = 1024 * 1024;

// High 32 bits tag the pattern as ours; low 32 hold the word index, so a 64-bit value read back
// from the wrong place identifies where it was written from.
#define PATTERN_TAG (0xD1AB0000ULL << 32)
#define PATTERN_MASK 0xFFFFFFFF00000000ULL

#define DIE(fmt, ...)                                       \
    do {                                                    \
        fprintf(stderr, "error: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                            \
    } while (0)

static double now_sec() {
    struct timespec ts {};

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

static void report(const char* label, size_t bytes, double dt) {
    printf(
        "%-22s %6zu MiB in %9.3f ms  (%6.2f GiB/s)\n",
        label,
        bytes / MIB,
        dt * 1e3,
        static_cast<double>(bytes) / dt / static_cast<double>(1ULL << 30));
    fflush(stdout);
}

static inline uint64_t pattern_word(uint64_t abs_addr) { return PATTERN_TAG | (abs_addr >> 3); }

// Turn an unexpected value into a diagnosis rather than a hex dump.
static void explain_mismatch(uint64_t abs_addr, uint64_t got, uint64_t want) {
    fprintf(
        stderr,
        "error: MISMATCH at NOC addr 0x%" PRIx64 ": got 0x%016" PRIx64 " want 0x%016" PRIx64 "\n",
        abs_addr,
        got,
        want);

    if (got == 0) {
        fprintf(stderr, "  reads back as zero: range is not backed, or writes are being dropped\n");
        return;
    }
    if (got == UINT64_MAX) {
        fprintf(stderr, "  reads back as all-ones: no responder, reads are returning a floating bus\n");
        return;
    }
    if ((got & PATTERN_MASK) == PATTERN_TAG) {
        uint64_t src_addr = (got & 0xFFFFFFFFULL) << 3;
        fprintf(
            stderr,
            "  this is the value written to NOC addr 0x%" PRIx64 " - the range ALIASES, 0x%" PRIx64 " and 0x%" PRIx64
            " are the same memory (delta 0x%" PRIx64 ")\n",
            src_addr,
            abs_addr,
            src_addr,
            abs_addr > src_addr ? abs_addr - src_addr : src_addr - abs_addr);
        return;
    }
    fprintf(stderr, "  value does not carry our pattern tag: foreign data or corruption\n");
}

int main(int argc, char** argv) {
    ChipId chip = 0;
    uint64_t addr = 0;
    size_t size = 4096 * MIB;
    size_t chunk = 64 * MIB;
    size_t bank = 0;
    bool verbose = false;
    bool one_pass = false;
    int opt;

    while ((opt = getopt(argc, argv, "c:a:s:k:b:v1h")) != -1) {
        switch (opt) {
            case 'c':
                chip = static_cast<ChipId>(atoi(optarg));
                break;
            case 'a':
                addr = strtoull(optarg, nullptr, 0);
                break;
            case 's':
                size = static_cast<size_t>(strtoull(optarg, nullptr, 0)) * MIB;
                break;
            case 'k':
                chunk = static_cast<size_t>(strtoull(optarg, nullptr, 0)) * MIB;
                break;
            case 'b':
                bank = static_cast<size_t>(strtoull(optarg, nullptr, 0));
                break;
            case 'v':
                verbose = true;
                break;
            case '1':
                one_pass = true;
                break;
            default:
                printf(
                    "usage: %s [options]\n"
                    "  -c N   chip id (default 0)\n"
                    "  -a N   NOC start address (default 0)\n"
                    "  -s N   size in MiB (default 4096)\n"
                    "  -k N   chunk size in MiB per read/write call (default 64)\n"
                    "  -b N   index into the DRAM core list (default 0)\n"
                    "  -v     print each chunk before accessing it (localizes a hang)\n"
                    "  -1     write+verify per chunk instead of write-all then read-all\n",
                    argv[0]);
                return opt == 'h' ? 0 : 2;
        }
    }

    if (size == 0 || chunk == 0) {
        DIE("size and chunk must be non-zero");
    }
    if (size % sizeof(uint64_t) != 0 || chunk % sizeof(uint64_t) != 0) {
        DIE("size and chunk must be multiples of 8");
    }

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->get_tt_device(chip)->set_power_state(true);

    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip);
    std::vector<CoreCoord> dram_cores = soc_desc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        DIE("no DRAM cores found on chip %d", static_cast<int>(chip));
    }
    if (bank >= dram_cores.size()) {
        DIE("bank index %zu out of range (%zu DRAM cores)", bank, dram_cores.size());
    }
    CoreCoord core = dram_cores[bank];

    printf(
        "chip %d core[%zu]/%zu=(x=%zu,y=%zu) addr=0x%" PRIx64 " size=%zu MiB chunk=%zu MiB\n",
        static_cast<int>(chip),
        bank,
        dram_cores.size(),
        core.x,
        core.y,
        addr,
        size / MIB,
        chunk / MIB);

    std::vector<uint64_t> buf(chunk / sizeof(uint64_t));
    size_t bad = 0;

    // A read to a NOC address that never responds does not return an error, it hangs - and it
    // leaves the board needing a reset. So every chunk announces itself before the access, flushed,
    // and the last line printed before a hang names the offset that hung.
    auto announce = [&](const char* what, size_t off, size_t len) {
        if (verbose) {
            printf("  %s off=0x%08zx len=%zu MiB addr=0x%" PRIx64 "\n", what, off, len / MIB, addr + off);
            fflush(stdout);
        }
    };

    auto fill = [&](size_t off, size_t len) {
        for (size_t j = 0; j < len / sizeof(uint64_t); j++) {
            buf[j] = pattern_word(addr + off + j * sizeof(uint64_t));
        }
    };

    auto compare = [&](size_t off, size_t len) {
        for (size_t j = 0; j < len / sizeof(uint64_t); j++) {
            uint64_t abs_addr = addr + off + j * sizeof(uint64_t);
            uint64_t want = pattern_word(abs_addr);

            if (buf[j] != want) {
                if (bad == 0) {
                    explain_mismatch(abs_addr, buf[j], want);
                }
                bad++;
            }
        }
    };

    double t0;
    double t1;

    if (one_pass) {
        // Write and verify one chunk at a time. Cannot detect aliasing (nothing else has been
        // written yet to be clobbered), but it stops at the first bad chunk instead of writing the
        // whole range first - which is what you want when writing the range is itself suspected of
        // wedging the device.
        t0 = now_sec();
        for (size_t off = 0; off < size; off += chunk) {
            size_t len = (size - off < chunk) ? size - off : chunk;

            fill(off, len);
            announce("W", off, len);
            cluster->write_to_device(buf.data(), len, chip, core, addr + off);

            memset(buf.data(), 0, len);
            announce("R", off, len);
            cluster->read_from_device(buf.data(), chip, core, addr + off, len);
            compare(off, len);

            if (bad) {
                fprintf(stderr, "stopping at first bad chunk (off=0x%zx)\n", off);
                break;
            }
        }
        t1 = now_sec();
        report("PIO write+read", size, t1 - t0);
    } else {
        // Phase 1: write the whole range before reading any of it, so aliasing shows up.
        t0 = now_sec();
        for (size_t off = 0; off < size; off += chunk) {
            size_t len = (size - off < chunk) ? size - off : chunk;

            fill(off, len);
            announce("W", off, len);
            cluster->write_to_device(buf.data(), len, chip, core, addr + off);
        }
        t1 = now_sec();
        report("PIO write", size, t1 - t0);

        // Phase 2: read it all back and compare against what each address should hold.
        t0 = now_sec();
        for (size_t off = 0; off < size; off += chunk) {
            size_t len = (size - off < chunk) ? size - off : chunk;

            memset(buf.data(), 0, len);
            announce("R", off, len);
            cluster->read_from_device(buf.data(), chip, core, addr + off, len);
            compare(off, len);
        }
        t1 = now_sec();
        report("PIO read", size, t1 - t0);
    }

    if (bad) {
        fprintf(
            stderr,
            "FAILED: %zu of %zu words wrong (%.2f%%)\n",
            bad,
            size / sizeof(uint64_t),
            100.0 * static_cast<double>(bad) / static_cast<double>(size / sizeof(uint64_t)));
        return 1;
    }

    printf("OK: %zu MiB at 0x%" PRIx64 " verified\n", size / MIB, addr);
    return 0;
}
