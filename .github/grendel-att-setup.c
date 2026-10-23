// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
//
// Programs the Grendel host path that bring-up firmware would have programmed, for a ttsim-backed
// endpoint under QEMU where it has not been. Runs in the guest before tt-kmd binds, because it
// reaches the registers through BAR2 itself and sysfs stops handing out the BAR once the driver
// claims the device.
//
// ttsim boots with neither of the two stages UMD's GRENDEL_QSR1_MAP assumes:
//   * TLB_APP_IN0 (inbound TLB entries 64..319, the 16 MB windows BAR0 decodes through) is entirely
//     invalid, so a BAR0 access dies in tlb_in() before it ever reaches the ATT.
//   * the d2d0 ATT holds one boot mask set sized for BAR4, not the QSR1 worker window.
// Both are reached through BAR2, which boot does leave usable.
//
// The endpoint words carry a +2 on each axis relative to UMD's tables. UMD is in the POR package
// frame (Quasar mesh origin (3,3)); ttsim's coord_to_tile reads the word in the soc-descriptor frame
// (origin (1,1)). Writing UMD's words verbatim silently lands 12 of 32 selectors on the wrong tile
// and gets the other 20 refused, so the shift is applied on the way in.

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TT_VENDOR_ID 0x1e52
#define KERAUNOS_DEVICE_ID 0xfeed

#define BAR2_SIZE (1u << 20)
#define WINDOW 0x4000u

// Inbound TLB entry k's config: PCIE_TLB (SMN 0x1804_0000) + 0x3000 + 64*k. Boot leaves BAR2 window 1
// on 0x1804_0000 and window 2 on 0x1804_4000, so entries 0..63 are in window 1 and 64..319 in window 2.
#define TLB_CFG_SYS (1 * WINDOW + 0x3000)
#define TLB_CFG_APP (2 * WINDOW)

// SPA 0x12_1C29_0000 -> pcie_remap region 2 -> SMN 0x1029_0000 = the d2d0 ATT.
#define ATT_SPA 0x121C290000ULL
#define ATT (63 * WINDOW)
#define ATT_MASK_ENTRY(set) (ATT + 0x030 + 0x18 * (set))
#define ATT_EP_ROW(row) (ATT + 0x2000 + 4 * (row))

// GRENDEL_QSR1_MAP, QSR1_WORKER_WINDOW.
#define W_COMPARE 0x10000000000ULL
#define W_MASK_BITS 30u
#define W_EP_SHIFT 24u
#define W_EP_SIZE 6u
#define W_TABLE_OFF 128u
#define W_SET 4u

static volatile uint32_t *bar2;

static void wr32(unsigned off, uint32_t v) {
    bar2[off / 4] = v;
    __sync_synchronize();
}

static void tlb_set(unsigned k, uint64_t spa) {
    unsigned cfg = (k < 64) ? (TLB_CFG_SYS + 64 * k) : (TLB_CFG_APP + 64 * (k - 64));
    wr32(cfg + 4, (uint32_t)(spa >> 32));
    wr32(cfg, (uint32_t)spa | 1u);
}

// Reads a sysfs attribute holding a single hex id, e.g. "0x1e52".
static long read_hex_attr(const char *dir, const char *attr) {
    char path[512];
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/%s", dir, attr);
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    long value = -1;
    if (fscanf(f, "%li", &value) != 1) {
        value = -1;
    }
    fclose(f);
    return value;
}

// The BDF of the first Keraunos function, so the workflow does not have to hard-code a slot.
static int find_keraunos(char *bdf, size_t len) {
    DIR *d = opendir("/sys/bus/pci/devices");
    if (!d) {
        perror("opendir /sys/bus/pci/devices");
        return -1;
    }
    int found = -1;
    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        if (read_hex_attr(e->d_name, "vendor") == TT_VENDOR_ID &&
            read_hex_attr(e->d_name, "device") == KERAUNOS_DEVICE_ID) {
            snprintf(bdf, len, "%s", e->d_name);
            found = 0;
            break;
        }
    }
    closedir(d);
    return found;
}

int main(int argc, char **argv) {
    char path[512];

    if (argc == 2) {
        snprintf(path, sizeof(path), "%s", argv[1]);
    } else if (argc == 1) {
        char bdf[256];
        if (find_keraunos(bdf, sizeof(bdf)) != 0) {
            fprintf(stderr, "grendel-att-setup: no %04x:%04x function is present\n", TT_VENDOR_ID,
                    KERAUNOS_DEVICE_ID);
            return 1;
        }
        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource2", bdf);
        printf("grendel-att-setup: using %s\n", path);
    } else {
        fprintf(stderr, "usage: %s [<sysfs resource2 path>]\n", argv[0]);
        return 2;
    }

    int fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    void *m = mmap(NULL, BAR2_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    bar2 = m;

    // Window 63 onto the ATT registers.
    tlb_set(63, ATT_SPA);

    // The whole 4 GB of BAR0 as 256 consecutive 16 MB windows onto the worker window, so BAR0 offset
    // s*16MB carries selector s exactly as UMD's make_address() puts it there. Only the first 64
    // selectors are meaningful (the window's selector field is 6 bits); the rest are pointed at
    // selector 0 so no entry is left invalid enough to _Exit the simulator.
    for (unsigned j = 0; j < 256; j++) {
        unsigned sel = j & 0x3F;
        tlb_set(64 + j, W_COMPARE + ((uint64_t)sel << W_EP_SHIFT));
    }

    // ttsim packs the window as mask[5:0], ep_id_idx[11:6], ep_id_size[17:12], offset[27:18].
    uint32_t entry = W_MASK_BITS | (W_EP_SHIFT << 6) | (W_EP_SIZE << 12) | (W_TABLE_OFF << 18);
    wr32(ATT_MASK_ENTRY(W_SET), entry);
    wr32(ATT_MASK_ENTRY(W_SET) + 0x08, (uint32_t)W_COMPARE);
    wr32(ATT_MASK_ENTRY(W_SET) + 0x0C, (uint32_t)(W_COMPARE >> 32));

    // The 32 NEO tiles, row-major, in ttsim's frame.
    for (unsigned s = 0; s < 32; s++) {
        unsigned x = 2 + (s % 8), y = 2 + (s / 8);
        wr32(ATT_EP_ROW(W_TABLE_OFF + s), (y << 6) | x);
    }

    printf("grendel-att-setup: BAR0 inbound TLB 64..319 valid, ATT mask set %u = 0x%08x "
           "(compare 0x%llx), endpoint rows %u..%u programmed\n",
           W_SET, entry, (unsigned long long)W_COMPARE, W_TABLE_OFF, W_TABLE_OFF + 31);
    munmap(m, BAR2_SIZE);
    close(fd);
    return 0;
}
