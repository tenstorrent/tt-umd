// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ============================================================================
// libttsim cooperative ABI (Part B prototype).
//
// This is the *contract* that a cooperative simulator .so exports. It is a pure
// C interface so it is stable across compilers and is the single source of truth
// shared between UMD and the sim team. Everything UMD needs to know about a sim
// is discoverable through these symbols at runtime -- there is no out-of-band
// convention (no sibling soc_descriptor.yaml, no file-extension dispatch, no
// per-feature dlsym probing).
//
// Versioning rule: bump LIBTTSIM_ABI_VERSION on ANY signature or semantic change.
// UMD asserts the loaded .so reports an ABI version it understands and refuses
// to run otherwise.
// ============================================================================

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI version UMD was compiled against. The .so reports its own via
// libttsim_abi_version(); UMD requires an exact major match (see backend).
#define LIBTTSIM_ABI_VERSION 1u

// Status codes returned by fallible entry points. Non-zero == failure; the
// human-readable detail is retrievable via libttsim_last_error().
typedef enum {
    LIBTTSIM_OK = 0,
    LIBTTSIM_ERR_INIT = 1,
    LIBTTSIM_ERR_BAD_ARG = 2,
    LIBTTSIM_ERR_UNSUPPORTED = 3,
    LIBTTSIM_ERR_BUFFER_TOO_SMALL = 4,
} libttsim_status_t;

// Self-declared feature bits. The sim advertises what it supports so UMD never
// has to probe individual symbol names to detect optional functionality.
typedef enum {
    LIBTTSIM_CAP_TILE_IO = 1ull << 0,    // libttsim_tile_{rd,wr}_bytes usable
    LIBTTSIM_CAP_PCI_MEM_IO = 1ull << 1,  // libttsim_pci_mem_{rd,wr}_bytes usable
    LIBTTSIM_CAP_PCI_CONFIG = 1ull << 2,  // libttsim_pci_config_rd32 usable
    LIBTTSIM_CAP_DMA_CALLBACKS = 1ull << 3,  // device-initiated DMA callbacks honored
    LIBTTSIM_CAP_MULTICHIP = 1ull << 4,   // >1 concurrent device handle supported
    LIBTTSIM_CAP_CLOCK = 1ull << 5,       // libttsim_clock advances simulation time
} libttsim_capability_t;

// Opaque per-device handle. Every I/O and callback registration carries this so
// the sim keeps its own per-device state; UMD holds no process-global statics.
typedef struct libttsim_device libttsim_device_t;

// DMA callback function-pointer types. The trailing void* is UMD's own per-device
// context, returned verbatim, so two devices in one process never collide.
typedef void (*libttsim_dma_rd_fn)(void* user, uint64_t sysmem_addr, void* dst, uint32_t size);
typedef void (*libttsim_dma_wr_fn)(void* user, uint64_t sysmem_addr, const void* src, uint32_t size);

// ---- Identity / compatibility (no handle: process-level facts) ----
typedef const char* (*pfn_libttsim_variant)(void);
typedef uint32_t (*pfn_libttsim_abi_version)(void);
typedef uint64_t (*pfn_libttsim_query_capabilities)(void);
typedef const char* (*pfn_libttsim_last_error)(void);

// ---- Lifecycle (graceful: returns status, sets last_error on failure) ----
typedef libttsim_status_t (*pfn_libttsim_init)(void);
typedef void (*pfn_libttsim_exit)(void);

// ---- Self-description: the .so tells UMD what it is modeling ----
// arch_out receives a tt::ARCH numeric value (WORMHOLE_B0=2, BLACKHOLE=3, QUASAR=4).
typedef libttsim_status_t (*pfn_libttsim_get_arch)(uint32_t* arch_out);
// Writes a full SOC descriptor (YAML text) into buf. If buf is too small, sets
// *needed to the required size and returns LIBTTSIM_ERR_BUFFER_TOO_SMALL.
typedef libttsim_status_t (*pfn_libttsim_get_soc_descriptor)(char* buf, size_t buf_size, size_t* needed);

// ---- Per-device lifecycle ----
typedef libttsim_status_t (*pfn_libttsim_create_device)(uint32_t device_id, libttsim_device_t** out);
typedef void (*pfn_libttsim_destroy_device)(libttsim_device_t* dev);

// ---- Per-device I/O (handle is first arg -- no select-before-I/O dance) ----
typedef uint32_t (*pfn_libttsim_pci_config_rd32)(libttsim_device_t* dev, uint32_t bdf, uint32_t offset);
typedef void (*pfn_libttsim_pci_mem_rd_bytes)(libttsim_device_t* dev, uint64_t paddr, void* dst, uint32_t size);
typedef void (*pfn_libttsim_pci_mem_wr_bytes)(libttsim_device_t* dev, uint64_t paddr, const void* src, uint32_t size);
typedef void (*pfn_libttsim_tile_rd_bytes)(
    libttsim_device_t* dev, uint32_t x, uint32_t y, uint64_t addr, void* dst, uint32_t size);
typedef void (*pfn_libttsim_tile_wr_bytes)(
    libttsim_device_t* dev, uint32_t x, uint32_t y, uint64_t addr, const void* src, uint32_t size);
typedef void (*pfn_libttsim_clock)(libttsim_device_t* dev, uint32_t n_clocks);

// ---- Per-device DMA callbacks (carry UMD's own user ptr -> no last-chip-wins bug) ----
typedef void (*pfn_libttsim_set_pci_dma_mem_callbacks)(
    libttsim_device_t* dev, libttsim_dma_rd_fn rd, libttsim_dma_wr_fn wr, void* user);

#ifdef __cplusplus
}  // extern "C"
#endif
