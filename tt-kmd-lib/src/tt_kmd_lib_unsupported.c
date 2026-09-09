// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>

#include "tt-kmd-lib/tt_kmd_lib.h"

// The kernel driver is Linux-only. Keep the C API linkable for simulator builds
// without exposing Linux ioctl definitions or pretending hardware I/O succeeded.

int tt_device_open(const char* chardev_path, tt_device_t** out_dev, int extra_flags) {
    (void)chardev_path;
    (void)out_dev;
    (void)extra_flags;
    return -ENOTSUP;
}

int tt_device_close(tt_device_t* dev) {
    (void)dev;
    return -ENOTSUP;
}

int tt_device_get_attrs(tt_device_t* dev, tt_device_attrs_t* out_attrs) {
    (void)dev;
    (void)out_attrs;
    return -ENOTSUP;
}

int tt_device_get_attr(tt_device_t* dev, enum tt_device_attr attr, uint64_t* out_value) {
    (void)dev;
    (void)attr;
    (void)out_value;
    return -ENOTSUP;
}

int tt_driver_get_attr(tt_device_t* dev, enum tt_driver_attr attr, uint64_t* out_value) {
    (void)dev;
    (void)attr;
    (void)out_value;
    return -ENOTSUP;
}

int tt_device_query_bar_mappings(tt_device_t* dev, tt_bar_mappings_t* out_mappings) {
    (void)dev;
    (void)out_mappings;
    return -ENOTSUP;
}

int tt_noc_read32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t* value) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    (void)value;
    return -ENOTSUP;
}

int tt_noc_write32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t value) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    (void)value;
    return -ENOTSUP;
}

int tt_noc_read(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, void* dst, size_t len) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    (void)dst;
    (void)len;
    return -ENOTSUP;
}

int tt_noc_write(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, const void* src, size_t len) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    (void)src;
    (void)len;
    return -ENOTSUP;
}

int tt_pin_pages(tt_device_t* dev, void* addr, size_t len, int flags, uint64_t* out_dma_addr, uint64_t* out_noc_addr) {
    (void)dev;
    (void)addr;
    (void)len;
    (void)flags;
    (void)out_dma_addr;
    (void)out_noc_addr;
    return -ENOTSUP;
}

int tt_unpin_pages(tt_device_t* dev, void* addr, size_t len) {
    (void)dev;
    (void)addr;
    (void)len;
    return -ENOTSUP;
}

int tt_dma_map(tt_device_t* dev, void* addr, size_t len, int flags, tt_dma_t** out_dma) {
    (void)dev;
    (void)addr;
    (void)len;
    (void)flags;
    (void)out_dma;
    return -ENOTSUP;
}

int tt_dma_unmap(tt_device_t* dev, tt_dma_t* dma) {
    (void)dev;
    (void)dma;
    return -ENOTSUP;
}

int tt_dma_get_dma_addr(tt_dma_t* dma, uint64_t* out_dma_addr) {
    (void)dma;
    (void)out_dma_addr;
    return -ENOTSUP;
}

int tt_dma_get_noc_addr(tt_dma_t* dma, uint64_t* out_noc_addr) {
    (void)dma;
    (void)out_noc_addr;
    return -ENOTSUP;
}

int tt_allocate_dma_buf(
    tt_device_t* dev,
    uint8_t buf_index,
    size_t size,
    int flags,
    void** out_mapping,
    uint64_t* out_dma_addr,
    uint64_t* out_noc_addr) {
    (void)dev;
    (void)buf_index;
    (void)size;
    (void)flags;
    (void)out_mapping;
    (void)out_dma_addr;
    (void)out_noc_addr;
    return -ENOTSUP;
}

int tt_tlb_alloc(tt_device_t* dev, size_t size, enum tt_tlb_cache_mode cache, tt_tlb_t** out_tlb) {
    (void)dev;
    (void)size;
    (void)cache;
    (void)out_tlb;
    return -ENOTSUP;
}

int tt_tlb_free(tt_device_t* dev, tt_tlb_t* tlb) {
    (void)dev;
    (void)tlb;
    return -ENOTSUP;
}

int tt_tlb_get_mmio(tt_tlb_t* tlb, void** out_mmio) {
    (void)tlb;
    (void)out_mmio;
    return -ENOTSUP;
}

int tt_tlb_get_id(tt_tlb_t* tlb, uint32_t* out_id) {
    (void)tlb;
    (void)out_id;
    return -ENOTSUP;
}

int tt_tlb_map(tt_device_t* dev, tt_tlb_t* tlb, tt_noc_addr_config_t* config) {
    (void)dev;
    (void)tlb;
    (void)config;
    return -ENOTSUP;
}

int tt_tlb_map_unicast(tt_device_t* dev, tt_tlb_t* tlb, uint8_t x, uint8_t y, uint64_t addr) {
    (void)dev;
    (void)tlb;
    (void)x;
    (void)y;
    (void)addr;
    return -ENOTSUP;
}

int tt_tlb_export_dmabuf(tt_device_t* dev, tt_tlb_t* tlb, uint64_t offset, uint64_t size, int* out_fd) {
    (void)dev;
    (void)tlb;
    (void)offset;
    (void)size;
    (void)out_fd;
    return -ENOTSUP;
}

int tt_device_set_power_state(tt_device_t* dev, uint16_t power_flags) {
    (void)dev;
    (void)power_flags;
    return -ENOTSUP;
}

int tt_device_reset(tt_device_t* dev, uint32_t reset_flags) {
    (void)dev;
    (void)reset_flags;
    return -ENOTSUP;
}

int tt_lock_acquire(tt_device_t* dev, uint8_t index, int* out_acquired) {
    (void)dev;
    (void)index;
    (void)out_acquired;
    return -ENOTSUP;
}

int tt_lock_release(tt_device_t* dev, uint8_t index, int* out_was_held) {
    (void)dev;
    (void)index;
    (void)out_was_held;
    return -ENOTSUP;
}

int tt_lock_test(tt_device_t* dev, uint8_t index, uint32_t* out_state) {
    (void)dev;
    (void)index;
    (void)out_state;
    return -ENOTSUP;
}
