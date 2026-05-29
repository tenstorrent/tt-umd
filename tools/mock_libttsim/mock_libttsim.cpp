// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// ============================================================================
// Mock cooperative libttsim.so for the Part B prototype.
//
// Stands in for the "already implemented" cooperative simulator. It exports the
// full libttsim ABI (see device/api/umd/device/simulation/backend/libttsim_abi.h)
// with trivial in-memory behavior so the UMD backend can compile, link, and load
// it. Each device handle is an independent in-memory object -- which is exactly
// what lets the UMD side avoid process-global statics.
// ============================================================================

#include "umd/device/simulation/backend/libttsim_abi.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace {

// Per-device state lives behind the opaque handle: a tiny tile/PCI memory map
// plus this device's own DMA callbacks and user pointer. No file-scope statics
// hold device state, so multiple devices coexist trivially.
constexpr uint32_t kVendorId = 0x1E52;
constexpr uint32_t kQuasarDeviceId = 0xFEED;

thread_local std::string g_last_error;

}  // namespace

struct libttsim_device {
    uint32_t id = 0;
    std::map<uint64_t, uint8_t> tile_mem;  // keyed by (x,y,addr) hash, simplified to addr
    std::map<uint64_t, uint8_t> pci_mem;
    libttsim_dma_rd_fn dma_rd = nullptr;
    libttsim_dma_wr_fn dma_wr = nullptr;
    void* dma_user = nullptr;
};

extern "C" {

const char* libttsim_variant(void) { return "ttsim-private"; }

uint32_t libttsim_abi_version(void) { return LIBTTSIM_ABI_VERSION; }

uint64_t libttsim_query_capabilities(void) {
    return LIBTTSIM_CAP_TILE_IO | LIBTTSIM_CAP_PCI_MEM_IO | LIBTTSIM_CAP_PCI_CONFIG | LIBTTSIM_CAP_DMA_CALLBACKS |
           LIBTTSIM_CAP_MULTICHIP | LIBTTSIM_CAP_CLOCK;
}

const char* libttsim_last_error(void) { return g_last_error.c_str(); }

libttsim_status_t libttsim_init(void) {
    g_last_error.clear();
    return LIBTTSIM_OK;
}

void libttsim_exit(void) {}

libttsim_status_t libttsim_get_arch(uint32_t* arch_out) {
    if (arch_out == nullptr) {
        g_last_error = "arch_out is null";
        return LIBTTSIM_ERR_BAD_ARG;
    }
    *arch_out = 4;  // tt::ARCH::QUASAR
    return LIBTTSIM_OK;
}

libttsim_status_t libttsim_get_soc_descriptor(char* buf, size_t buf_size, size_t* needed) {
    // Minimal but representative SOC descriptor text. A real sim emits the full
    // descriptor for the topology it is modeling.
    static const char* kYaml =
        "arch_name: QUASAR\n"
        "grid:\n"
        "  x_size: 8\n"
        "  y_size: 8\n";
    size_t len = std::strlen(kYaml);
    if (needed != nullptr) {
        *needed = len;
    }
    if (buf == nullptr || buf_size < len + 1) {
        g_last_error = "buffer too small for soc descriptor";
        return LIBTTSIM_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buf, kYaml, len + 1);
    return LIBTTSIM_OK;
}

libttsim_status_t libttsim_create_device(uint32_t device_id, libttsim_device_t** out) {
    if (out == nullptr) {
        g_last_error = "out is null";
        return LIBTTSIM_ERR_BAD_ARG;
    }
    auto* dev = new libttsim_device();
    dev->id = device_id;
    *out = dev;
    return LIBTTSIM_OK;
}

void libttsim_destroy_device(libttsim_device_t* dev) { delete dev; }

uint32_t libttsim_pci_config_rd32(libttsim_device_t* dev, uint32_t /*bdf*/, uint32_t offset) {
    (void)dev;
    if (offset == 0) {
        // [device_id:16 | vendor_id:16]
        return (kQuasarDeviceId << 16) | kVendorId;
    }
    return 0;
}

void libttsim_pci_mem_rd_bytes(libttsim_device_t* dev, uint64_t paddr, void* dst, uint32_t size) {
    auto* bytes = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < size; ++i) {
        auto it = dev->pci_mem.find(paddr + i);
        bytes[i] = (it == dev->pci_mem.end()) ? 0 : it->second;
    }
}

void libttsim_pci_mem_wr_bytes(libttsim_device_t* dev, uint64_t paddr, const void* src, uint32_t size) {
    const auto* bytes = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < size; ++i) {
        dev->pci_mem[paddr + i] = bytes[i];
    }
}

void libttsim_tile_rd_bytes(
    libttsim_device_t* dev, uint32_t x, uint32_t y, uint64_t addr, void* dst, uint32_t size) {
    uint64_t key = (uint64_t(x) << 56) ^ (uint64_t(y) << 48) ^ addr;
    auto* bytes = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < size; ++i) {
        auto it = dev->tile_mem.find(key + i);
        bytes[i] = (it == dev->tile_mem.end()) ? 0 : it->second;
    }
}

void libttsim_tile_wr_bytes(
    libttsim_device_t* dev, uint32_t x, uint32_t y, uint64_t addr, const void* src, uint32_t size) {
    uint64_t key = (uint64_t(x) << 56) ^ (uint64_t(y) << 48) ^ addr;
    const auto* bytes = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < size; ++i) {
        dev->tile_mem[key + i] = bytes[i];
    }
}

void libttsim_clock(libttsim_device_t* /*dev*/, uint32_t /*n_clocks*/) {
    // No-op in the mock; a real sim would advance simulation time.
}

void libttsim_set_pci_dma_mem_callbacks(
    libttsim_device_t* dev, libttsim_dma_rd_fn rd, libttsim_dma_wr_fn wr, void* user) {
    // Stored per-device, alongside the user pointer -- the mock would invoke
    // rd(user, ...)/wr(user, ...) when the modeled device touches host memory.
    dev->dma_rd = rd;
    dev->dma_wr = wr;
    dev->dma_user = user;
}

}  // extern "C"
