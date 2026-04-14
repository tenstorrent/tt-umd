// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_kmd_lib.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/mman.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ioctl.h"
#include "pci_ids.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define DEBUG(fmt, ...)                                                        \
    do {                                                                       \
        fprintf(stderr, "%s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)

static uint64_t TLB_COUNT_1M[] = {
    [TT_DEVICE_ARCH_UNKNOWN] = 0,
    [TT_DEVICE_ARCH_WORMHOLE] = 156,
    [TT_DEVICE_ARCH_BLACKHOLE] = 0,
};

static uint64_t TLB_COUNT_2M[] = {
    [TT_DEVICE_ARCH_UNKNOWN] = 0,
    [TT_DEVICE_ARCH_WORMHOLE] = 10,
    [TT_DEVICE_ARCH_BLACKHOLE] = 202,
};

static uint64_t TLB_COUNT_16M[] = {
    [TT_DEVICE_ARCH_UNKNOWN] = 0,
    [TT_DEVICE_ARCH_WORMHOLE] = 20,
    [TT_DEVICE_ARCH_BLACKHOLE] = 0,
};

static uint64_t TLB_COUNT_4G[] = {
    [TT_DEVICE_ARCH_UNKNOWN] = 0, [TT_DEVICE_ARCH_WORMHOLE] = 0, [TT_DEVICE_ARCH_BLACKHOLE] = 8};

struct tt_device_t {
    int fd;
};

struct tt_tlb_t {
    uint32_t id;
    size_t size;
    void* mmio;
};

struct tt_dma_t {
    void* addr;    /* Virtual address */
    size_t len;    /* Bytes */
    uint64_t iova; /* I/O Virtual Address */
    uint64_t noc;  /* NOC address (inside EP PCIe tile) */
};

/* -------------------------------------------------------------------------
 * Device lifecycle
 * ---------------------------------------------------------------------- */

int tt_device_open(const char* chardev_path, tt_device_t** out_dev, int extra_flags) {
    struct tt_device_t* dev = calloc(1, sizeof(struct tt_device_t));

    if (!dev) {
        return -ENOMEM;
    }

    dev->fd = open(chardev_path, O_RDWR | O_CLOEXEC | extra_flags);
    if (dev->fd == -1) {
        int e = errno;
        free(dev);
        return -e;
    }

    *out_dev = dev;

    return 0;
}

int tt_device_close(tt_device_t* dev) {
    if (close(dev->fd) != 0) {
        return -errno;
    }

    free(dev);
    return 0;
}

int tt_device_get_fd(tt_device_t* dev, int* out_fd) {
    *out_fd = dev->fd;
    return 0;
}

int tt_device_get_info_fd(int fd, tt_device_info_t* out) {
    struct tenstorrent_get_device_info info = {0};
    info.in.output_size_bytes = sizeof(info.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) != 0) {
        return -errno;
    }

    out->vendor_id = info.out.vendor_id;
    out->device_id = info.out.device_id;
    out->subsystem_vendor_id = info.out.subsystem_vendor_id;
    out->subsystem_id = info.out.subsystem_id;
    out->pci_domain = info.out.pci_domain;
    out->bus_dev_fn = info.out.bus_dev_fn;

    return 0;
}

int tt_device_get_info(tt_device_t* dev, tt_device_info_t* out) { return tt_device_get_info_fd(dev->fd, out); }

int tt_device_get_attr(tt_device_t* dev, enum tt_device_attr attr, uint64_t* out_value) {
    struct tenstorrent_get_device_info get_device_info = {0};

    get_device_info.in.output_size_bytes = sizeof(get_device_info.out);

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &get_device_info) != 0) {
        return -errno;
    }

    uint64_t arch = TT_DEVICE_ARCH_UNKNOWN;
    if (get_device_info.out.device_id == TT_BLACKHOLE_PCI_DEVICE_ID) {
        arch = TT_DEVICE_ARCH_BLACKHOLE;
    } else if (get_device_info.out.device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
        arch = TT_DEVICE_ARCH_WORMHOLE;
    }

    switch (attr) {
        case TT_DEVICE_ATTR_PCI_DOMAIN:
            *out_value = get_device_info.out.pci_domain;
            break;
        case TT_DEVICE_ATTR_PCI_BUS:
            *out_value = get_device_info.out.bus_dev_fn >> 8;
            break;
        case TT_DEVICE_ATTR_PCI_DEVICE:
            *out_value = (get_device_info.out.bus_dev_fn >> 3) & 0x1F;
            break;
        case TT_DEVICE_ATTR_PCI_FUNCTION:
            *out_value = get_device_info.out.bus_dev_fn & 0x07;
            break;
        case TT_DEVICE_ATTR_PCI_VENDOR_ID:
            *out_value = get_device_info.out.vendor_id;
            break;
        case TT_DEVICE_ATTR_PCI_DEVICE_ID:
            *out_value = get_device_info.out.device_id;
            break;
        case TT_DEVICE_ATTR_PCI_SUBSYSTEM_ID:
            *out_value = get_device_info.out.subsystem_id;
            break;
        case TT_DEVICE_ATTR_CHIP_ARCH:
            *out_value = arch;
            break;
        case TT_DEVICE_ATTR_NUM_1M_TLBS:
            *out_value = TLB_COUNT_1M[arch];
            break;
        case TT_DEVICE_ATTR_NUM_2M_TLBS:
            *out_value = TLB_COUNT_2M[arch];
            break;
        case TT_DEVICE_ATTR_NUM_16M_TLBS:
            *out_value = TLB_COUNT_16M[arch];
            break;
        case TT_DEVICE_ATTR_NUM_4G_TLBS:
            *out_value = TLB_COUNT_4G[arch];
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

int tt_driver_get_attr(tt_device_t* dev, enum tt_driver_attr attr, uint64_t* out_value) {
    struct tenstorrent_get_driver_info get_driver_info = {0};
    get_driver_info.in.output_size_bytes = sizeof(get_driver_info.out);

    /* OK to call with NULL dev, but can't return semver without a device. */
    if (dev) {
        if (ioctl(dev->fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &get_driver_info) != 0) {
            return -errno;
        }
    }

    switch (attr) {
        case TT_DRIVER_API_VERSION:
            *out_value = TENSTORRENT_DRIVER_VERSION;
            return 0;
        case TT_DRIVER_SEMVER_MAJOR:
            *out_value = get_driver_info.out.driver_version_major;
            return dev ? 0 : -ENODEV;
        case TT_DRIVER_SEMVER_MINOR:
            *out_value = get_driver_info.out.driver_version_minor;
            return dev ? 0 : -ENODEV;
        case TT_DRIVER_SEMVER_PATCH:
            *out_value = get_driver_info.out.driver_version_patch;
            return dev ? 0 : -ENODEV;
        default:
            return -EINVAL;
    }

    return 0;
}

int tt_query_mappings(tt_device_t* dev, tt_bar_mapping_t* out_mappings, uint32_t count, uint32_t* out_count) {
    /* The kernel ioctl lays the mapping array immediately after the query struct in memory. */
    struct {
        struct tenstorrent_query_mappings query;
        struct tenstorrent_mapping array[8];
    } buf;

    if (count > 8) {
        count = 8;
    }

    memset(&buf, 0, sizeof(buf));
    buf.query.in.output_mapping_count = count;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &buf.query) != 0) {
        return -errno;
    }

    uint32_t filled = 0;
    for (uint32_t i = 0; i < count; i++) {
        out_mappings[i].mapping_id = buf.array[i].mapping_id;
        out_mappings[i].mapping_base = buf.array[i].mapping_base;
        out_mappings[i].mapping_size = buf.array[i].mapping_size;
        if (buf.array[i].mapping_id != TENSTORRENT_MAPPING_UNUSED) {
            filled = i + 1;
        }
    }

    *out_count = filled;
    return 0;
}

int tt_device_reset(tt_device_t* dev, uint32_t flags) {
    struct tenstorrent_reset_device reset_info = {0};
    reset_info.in.output_size_bytes = sizeof(reset_info.out);
    reset_info.in.flags = flags;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset_info) != 0) {
        return -errno;
    }

    return 0;
}

int tt_set_power_state(tt_device_t* dev, uint16_t power_flags) {
    struct tenstorrent_power_state ps = {0};
    ps.argsz = sizeof(ps);
    ps.validity = TT_POWER_VALIDITY(4, 0);
    ps.power_flags = power_flags;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_SET_POWER_STATE, &ps) != 0) {
        return -errno;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * NOC access (convenience functions)
 * ---------------------------------------------------------------------- */

int tt_noc_read32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t* value) {
    if (addr % 4 != 0) {
        return -EINVAL;
    }

    tt_tlb_t* tlb = NULL;
    int ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_UC, &tlb);
    if (ret != 0) {
        return ret;
    }

    uint64_t aligned_addr = addr & ~(tlb->size - 1);
    ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);

    if (ret != 0) {
        tt_tlb_free(dev, tlb);
        return ret;
    }

    uint64_t offset = addr & (tlb->size - 1);
    *value = *(volatile uint32_t*)((uint8_t*)tlb->mmio + offset);

    tt_tlb_free(dev, tlb);
    return 0;
}

int tt_noc_write32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t value) {
    if (addr % 4 != 0) {
        return -EINVAL;
    }

    tt_tlb_t* tlb = NULL;
    int ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_UC, &tlb);
    if (ret != 0) {
        return ret;
    }

    uint64_t aligned_addr = addr & ~(tlb->size - 1);
    ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);

    if (ret != 0) {
        tt_tlb_free(dev, tlb);
        return ret;
    }

    uint64_t offset = addr & (tlb->size - 1);
    *(volatile uint32_t*)((uint8_t*)tlb->mmio + offset) = value;

    tt_tlb_free(dev, tlb);
    return 0;
}

int tt_noc_read(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, void* dst, size_t len) {
    uint8_t* dst_ptr = (uint8_t*)dst;
    tt_tlb_t* tlb = NULL;
    int32_t ret;

    if (addr % 4 != 0 || len % 4 != 0) {
        return -EINVAL;
    }

    ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_WC, &tlb);
    if (ret != 0) {
        return ret;
    }

    while (len > 0) {
        uint64_t aligned_addr = addr & ~(tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(len, tlb->size - offset);
        uint8_t* src_ptr = (uint8_t*)tlb->mmio + offset;

        ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);

        if (ret != 0) {
            tt_tlb_free(dev, tlb);
            return ret;
        }

        if (chunk_size > 0 && offset + chunk_size <= tlb->size) {
            memcpy(dst_ptr, src_ptr, chunk_size);
        } else {
            tt_tlb_free(dev, tlb);
            return -EINVAL;
        }

        dst_ptr += chunk_size;
        len -= chunk_size;
        addr += chunk_size;
    }

    tt_tlb_free(dev, tlb);

    return 0;
}

int tt_noc_write(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, const void* src, size_t len) {
    const uint8_t* src_ptr = (const uint8_t*)src;
    tt_tlb_t* tlb = NULL;
    int32_t ret;

    if (addr % 4 != 0 || len % 4 != 0) {
        return -EINVAL;
    }

    ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_WC, &tlb);
    if (ret != 0) {
        return ret;
    }

    while (len > 0) {
        uint64_t aligned_addr = addr & ~(tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(len, tlb->size - offset);
        uint8_t* dst_ptr = (uint8_t*)tlb->mmio + offset;

        ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);

        if (ret != 0) {
            tt_tlb_free(dev, tlb);
            return ret;
        }

        if (chunk_size > 0 && offset + chunk_size <= tlb->size) {
            memcpy(dst_ptr, src_ptr, chunk_size);
        } else {
            tt_tlb_free(dev, tlb);
            return -EINVAL;
        }

        src_ptr += chunk_size;
        len -= chunk_size;
        addr += chunk_size;
    }

    tt_tlb_free(dev, tlb);

    return 0;
}

/* -------------------------------------------------------------------------
 * DMA — high-level lifecycle-managed API
 * ---------------------------------------------------------------------- */

int tt_dma_map(tt_device_t* dev, void* addr, size_t len, int flags, tt_dma_t** out_dma) {
    int page_size = getpagesize();
    if (len == 0 || len % page_size != 0 || addr == NULL || (uint64_t)addr % page_size != 0) {
        return -EINVAL;
    }

    struct tt_dma_t* dma = calloc(1, sizeof(struct tt_dma_t));

    if (!dma) {
        return -ENOMEM;
    }

    struct {
        struct tenstorrent_pin_pages_in in;
        struct tenstorrent_pin_pages_out_extended out;
    } pin_pages;

    memset(&pin_pages, 0, sizeof(pin_pages));

    pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
    pin_pages.in.virtual_address = (uint64_t)addr;
    pin_pages.in.size = len;

    if (flags & TT_DMA_FLAG_CONTIGUOUS) {
        pin_pages.in.flags |= TENSTORRENT_PIN_PAGES_CONTIGUOUS;
    }
    if (flags & TT_DMA_FLAG_NOC) {
        pin_pages.in.flags |= TENSTORRENT_PIN_PAGES_NOC_DMA;
    } else if (flags & TT_DMA_FLAG_NOC_TOP_DOWN) {
        pin_pages.in.flags |= TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN;
    }

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) != 0) {
        int e = errno;
        free(dma);
        return -e;
    }

    dma->addr = addr;
    dma->len = len;
    dma->iova = pin_pages.out.physical_address;

    if (flags & (TT_DMA_FLAG_NOC | TT_DMA_FLAG_NOC_TOP_DOWN)) {
        dma->noc = pin_pages.out.noc_address;
    } else {
        dma->noc = ~0ULL;
    }

    *out_dma = dma;

    return 0;
}

int tt_dma_unmap(tt_device_t* dev, tt_dma_t* dma) {
    struct tenstorrent_unpin_pages unpin = {0};
    unpin.in.virtual_address = (uint64_t)dma->addr;
    unpin.in.size = dma->len;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
        return -errno;
    }

    free(dma);

    return 0;
}

int tt_dma_get_dma_addr(tt_dma_t* dma, uint64_t* out_dma_addr) {
    *out_dma_addr = dma->iova;
    return 0;
}

int tt_dma_get_noc_addr(tt_dma_t* dma, uint64_t* out_noc_addr) {
    if (dma->noc == ~0ULL) {
        return -EINVAL;
    }

    *out_noc_addr = dma->noc;
    return 0;
}

/* -------------------------------------------------------------------------
 * DMA — low-level fire-and-forget pin/unpin
 * ---------------------------------------------------------------------- */

int tt_pin_pages(tt_device_t* dev, uint64_t va, size_t len, int flags, uint64_t* out_pa) {
    struct tenstorrent_pin_pages pin = {0};
    pin.in.output_size_bytes = sizeof(pin.out);
    pin.in.virtual_address = va;
    pin.in.size = len;

    if (flags & TT_DMA_FLAG_CONTIGUOUS) {
        pin.in.flags |= TENSTORRENT_PIN_PAGES_CONTIGUOUS;
    }

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
        return -errno;
    }

    *out_pa = pin.out.physical_address;
    return 0;
}

int tt_pin_pages_noc(tt_device_t* dev, uint64_t va, size_t len, int flags, uint64_t* out_noc_addr, uint64_t* out_pa) {
    struct {
        struct tenstorrent_pin_pages_in in;
        struct tenstorrent_pin_pages_out_extended out;
    } pin;

    memset(&pin, 0, sizeof(pin));
    pin.in.output_size_bytes = sizeof(pin.out);
    pin.in.virtual_address = va;
    pin.in.size = len;

    if (flags & TT_DMA_FLAG_CONTIGUOUS) {
        pin.in.flags |= TENSTORRENT_PIN_PAGES_CONTIGUOUS;
    }
    if (flags & TT_DMA_FLAG_NOC_TOP_DOWN) {
        pin.in.flags |= TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN;
    } else {
        pin.in.flags |= TENSTORRENT_PIN_PAGES_NOC_DMA;
    }

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
        return -errno;
    }

    *out_noc_addr = pin.out.noc_address;
    *out_pa = pin.out.physical_address;
    return 0;
}

int tt_unpin_pages(tt_device_t* dev, uint64_t va, size_t len) {
    struct tenstorrent_unpin_pages unpin = {0};
    unpin.in.virtual_address = va;
    unpin.in.size = len;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
        return -errno;
    }

    return 0;
}

int tt_allocate_dma_buf(
    tt_device_t* dev,
    uint32_t size,
    uint8_t buf_index,
    uint64_t* out_pa,
    uint64_t* out_mmap_offset,
    uint32_t* out_size) {
    struct tenstorrent_allocate_dma_buf dma_buf = {0};
    dma_buf.in.requested_size = size;
    dma_buf.in.buf_index = buf_index;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dma_buf) != 0) {
        return -errno;
    }

    *out_pa = dma_buf.out.physical_address;
    *out_mmap_offset = dma_buf.out.mapping_offset;
    *out_size = dma_buf.out.size;
    return 0;
}

/* -------------------------------------------------------------------------
 * TLB management
 * ---------------------------------------------------------------------- */

int tt_tlb_alloc(tt_device_t* dev, size_t size, enum tt_tlb_cache_mode cache, tt_tlb_t** out_tlb) {
    struct tt_tlb_t* tlb = malloc(sizeof(struct tt_tlb_t));

    if (!tlb) {
        return -ENOMEM;
    }

    memset(tlb, 0, sizeof(struct tt_tlb_t));

    struct tenstorrent_allocate_tlb alloc_tlb = {0};
    alloc_tlb.in.size = size;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc_tlb) != 0) {
        int e = errno;
        free(tlb);
        return -e;
    }

    off_t offset = cache == TT_MMIO_CACHE_MODE_UC ? alloc_tlb.out.mmap_offset_uc : alloc_tlb.out.mmap_offset_wc;
    tlb->id = alloc_tlb.out.id;
    tlb->size = size;
    tlb->mmio = mmap(NULL, tlb->size, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, offset);

    if (tlb->mmio == MAP_FAILED) {
        struct tenstorrent_free_tlb free_tlb = {0};
        int e = errno;
        free_tlb.in.id = tlb->id;
        if (ioctl(dev->fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb) != 0) {
            fprintf(stderr, "Leaked TLB %u: after mmap failure: %s\n", tlb->id, strerror(errno));
        }

        free(tlb);
        errno = e;
        return -e;
    }

    *out_tlb = tlb;

    return 0;
}

int tt_tlb_free(tt_device_t* dev, tt_tlb_t* tlb) {
    int ret = 0;

    munmap(tlb->mmio, tlb->size);

    struct tenstorrent_free_tlb free_tlb = {0};
    free_tlb.in.id = tlb->id;
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb) != 0) {
        ret = -errno;
    }

    free(tlb);

    return ret;
}

int tt_tlb_get_mmio(tt_tlb_t* tlb, void** out_mmio) {
    *out_mmio = tlb->mmio;
    return 0;
}

int tt_tlb_get_id(tt_tlb_t* tlb, uint32_t* out_id) {
    *out_id = tlb->id;
    return 0;
}

int tt_tlb_map(tt_device_t* dev, tt_tlb_t* tlb, tt_noc_addr_config_t* config) {
    struct tenstorrent_configure_tlb configure_tlb = {0};

    if (config->addr & (tlb->size - 1)) {
        return -EINVAL;
    }

    configure_tlb.in.id = tlb->id;
    configure_tlb.in.config.addr = config->addr;
    configure_tlb.in.config.x_end = config->x_end;
    configure_tlb.in.config.y_end = config->y_end;
    configure_tlb.in.config.x_start = config->x_start;
    configure_tlb.in.config.y_start = config->y_start;
    configure_tlb.in.config.noc = config->noc;
    configure_tlb.in.config.mcast = config->mcast;
    configure_tlb.in.config.ordering = config->ordering;
    configure_tlb.in.config.static_vc = config->static_vc;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
        return -errno;
    }

    return 0;
}

int tt_tlb_map_unicast(tt_device_t* dev, tt_tlb_t* tlb, uint8_t x, uint8_t y, uint64_t addr) {
    struct tenstorrent_configure_tlb configure_tlb = {0};

    if (addr & (tlb->size - 1)) {
        return -EINVAL;
    }

    configure_tlb.in.id = tlb->id;
    configure_tlb.in.config.addr = addr;
    configure_tlb.in.config.x_end = x;
    configure_tlb.in.config.y_end = y;

    if (ioctl(dev->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
        return -errno;
    }

    return 0;
}
