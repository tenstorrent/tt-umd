// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>

namespace tt::umd {

/**
 * TTSimCommunicator handles low-level communication with the TTSim .so library.
 * It manages dynamic library loading, function pointer resolution, and provides
 * thread-safe access to simulator functions.
 *
 * This class can be used independently of TTSimTTDevice for direct simulator communication.
 */
class TTSimCommunicator final {
public:
    /**
     * Constructor for TTSimCommunicator.
     *
     * @param simulator_directory Path to the simulator binary/directory
     * @param copy_sim_binary If true, copy the simulator binary to memory for
     *   security AND the loaded .so doesn't support the v3.5 chip-id ABI. If
     *   the .so exports libttsim_create_device_by_id, v3.5 shared-library
     *   mode is auto-enabled at initialize() time, ignoring this flag.
     * @param chip_id Logical chip ID (0..N-1) within the cluster. Only used
     *   in v3.5 multichip mode (per docs/v3_5_umd_patch_sketch.md). Default 0
     *   for legacy single-chip consumers.
     */
    TTSimCommunicator(
        const std::filesystem::path &simulator_directory, bool copy_sim_binary = false, uint32_t chip_id = 0);

    /**
     * Destructor that properly cleans up library handles and file descriptors.
     */
    ~TTSimCommunicator();

    /**
     * Initialize the simulator and establish communication.
     * Must be called before using any communication methods.
     * This loads the library, resolves function pointers, and starts the simulator.
     */
    void initialize();

    /**
     * Shutdown the simulator and close communication.
     */
    void shutdown();

    /**
     * Read data from a tile core.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to read from
     * @param data Buffer to store read data
     * @param size Number of bytes to read
     */
    void tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size);

    /**
     * Write data to a tile core.
     *
     * @param x Core X coordinate
     * @param y Core Y coordinate
     * @param addr Address to write to
     * @param data Data to write
     * @param size Number of bytes to write
     */
    void tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size);

    /**
     * Fast simulator-only DRAM access. Returns false when the loaded simulator
     * does not export the direct DRAM ABI, allowing callers to fall back to the
     * normal tile/TLB path.
     */
    bool dram_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size);
    bool dram_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size);

    /**
     * Read data from PCI memory.
     *
     * @param paddr Physical address
     * @param data Buffer to store read data
     * @param size Number of bytes to read
     */
    void pci_mem_read_bytes(uint64_t paddr, void *data, uint32_t size);

    /**
     * Write data to PCI memory.
     *
     * @param paddr Physical address
     * @param data Data to write
     * @param size Number of bytes to write
     */
    void pci_mem_write_bytes(uint64_t paddr, const void *data, uint32_t size);

    /**
     * Read from PCI configuration space.
     *
     * @param bus_device_function Bus/device/function identifier
     * @param offset Offset in configuration space
     * @return 32-bit value read from configuration space
     */
    uint32_t pci_config_read32(uint32_t bus_device_function, uint32_t offset);

    /**
     * Advance the simulator clock.
     *
     * @param n_clocks Number of clock cycles to advance
     */
    void advance_clock(uint32_t n_clocks);

    /**
     * Set callbacks for PCIe DMA memory operations. These callbacks are called
     * by TTSim when device performs NOC reads/writes to PCIe core.
     * These functions should basically implement how we handle copying data to/from system memory
     * when transactions are initiated by device.
     *
     * @param pfn_pci_dma_mem_rd_bytes Callback for PCIe DMA read operations, with parameters (system bus address,
     * buffer pointer, size).
     * @param pfn_pci_dma_mem_wr_bytes Callback for PCIe DMA write operations, with parameters (system bus address,
     * buffer pointer, size).
     */
    void set_pcie_dma_mem_callbacks(
        std::function<void(uint64_t, void *, uint32_t)> pfn_pci_dma_mem_rd_bytes,
        std::function<void(uint64_t, const void *, uint32_t)> pfn_pci_dma_mem_wr_bytes);

    void start_sim();

    // v3.5 commit #6 — eth-MAC wiring.
    void *get_dev_handle() const { return dev_handle_; }

    void switch_reset();
    void register_eth_endpoint(uint32_t eth_tile_id, uint64_t mac);
    void switch_drain();
    void register_peer(uint32_t eth_tile_id, void *peer_dev, uint32_t peer_tile_id);
    void register_fabric_node_id(uint32_t mesh_id, uint32_t chip_id);
    void register_fabric_endpoint_direction(uint32_t eth_tile_id, uint32_t direction);

private:
    // Library management.
    void create_simulator_binary();
    off_t resize_simulator_binary(int src_fd);
    void copy_simulator_binary();
    void secure_simulator_binary();
    void close_simulator_binary();
    void load_simulator_library(const std::filesystem::path &path);

    // Dynamic library handle.
    void *libttsim_handle_ = nullptr;

    // File descriptor for copied simulator binary.
    int copied_simulator_fd_ = -1;

    // Simulator directory path.
    std::filesystem::path simulator_directory_;

    // Flag to indicate if binary should be copied to memory.
    bool copy_sim_binary_;

    // v3.5 multi-chip mode: when v3.5 symbols are present in the .so, all
    // TTSimCommunicators sharing the same simulator_directory_ share one
    // dlopen (held via s_shared_handle_ + refcounted via s_shared_refcount_).
    // Each communicator records its chip_id_ and calls
    // libttsim_select_device_by_id(chip_id_) before every I/O.
    bool v3_5_multichip_mode_ = false;
    uint32_t chip_id_ = 0;
    static void *s_shared_handle_;
    static int s_shared_refcount_;
    static bool s_sim_initialized_;
    static std::mutex s_shared_init_mutex_;

    // Function pointers to simulator library functions.
    void (*pfn_libttsim_init_)() = nullptr;
    void (*pfn_libttsim_exit_)() = nullptr;
    uint32_t (*pfn_libttsim_pci_config_rd32_)(uint32_t bus_device_function, uint32_t offset) = nullptr;
    void (*pfn_libttsim_pci_mem_rd_bytes_)(uint64_t paddr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_pci_mem_wr_bytes_)(uint64_t paddr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_rd_bytes_)(uint32_t x, uint32_t y, uint64_t addr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes_)(uint32_t x, uint32_t y, uint64_t addr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_dram_rd_bytes_by_id_)(
        uint32_t chip_id, uint32_t dram_channel, uint64_t addr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_dram_wr_bytes_by_id_)(
        uint32_t chip_id, uint32_t dram_channel, uint64_t addr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_dram_core_rd_bytes_by_id_)(
        uint32_t chip_id, uint32_t x, uint32_t y, uint64_t addr, void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_dram_core_wr_bytes_by_id_)(
        uint32_t chip_id, uint32_t x, uint32_t y, uint64_t addr, const void *p, uint32_t size) = nullptr;
    void (*pfn_libttsim_clock_)(uint32_t n_clocks) = nullptr;
    void (*pfn_libttsim_set_pci_dma_mem_callbacks_)(
        void (*pfn_pci_dma_mem_rd_bytes)(uint64_t paddr, void *p, uint32_t size),
        void (*pfn_pci_dma_mem_wr_bytes)(uint64_t paddr, const void *p, uint32_t size)) = nullptr;
    void (*pfn_libttsim_set_pci_dma_mem_callbacks_by_chip_)(
        void (*pfn_pci_dma_mem_rd_bytes)(uint32_t chip_id, uint64_t paddr, void *p, uint32_t size),
        void (*pfn_pci_dma_mem_wr_bytes)(uint32_t chip_id, uint64_t paddr, const void *p, uint32_t size)) = nullptr;

    // v3.5 multi-chip ABI. Resolved via dlsym; nullptr if .so is legacy single-chip.
    void *(*pfn_libttsim_create_device_by_id_)(uint32_t chip_id, int chip_x, int chip_y) = nullptr;
    void (*pfn_libttsim_select_device_by_id_)(uint32_t chip_id) = nullptr;
    void (*pfn_libttsim_clock_all_devices_)(uint32_t n_cycles) = nullptr;

    // v3.5 commit #6 — eth-MAC wiring.
    void *dev_handle_ = nullptr;
    void (*pfn_libttsim_switch_reset_)() = nullptr;
    void (*pfn_libttsim_switch_register_)(void *dev, uint32_t tile_id, uint64_t mac) = nullptr;
    void (*pfn_libttsim_configure_eth_link_virtual_)(void *dev, uint32_t tile_id, uint64_t local_mac) = nullptr;
    void (*pfn_libttsim_switch_register_peer_)(void *dev, uint32_t tile_id, void *peer_dev, uint32_t peer_tile_id) =
        nullptr;
    void (*pfn_libttsim_switch_register_fabric_node_id_)(void *dev, uint32_t mesh_id, uint32_t chip_id) = nullptr;
    void (*pfn_libttsim_switch_register_fabric_endpoint_direction_)(void *dev, uint32_t tile_id, uint32_t direction) =
        nullptr;
    void (*pfn_libttsim_switch_drain_)() = nullptr;

    // Stored callbacks for DMA memory operations.
    std::function<void(uint64_t, void *, uint32_t)> pci_dma_mem_rd_bytes_callback_;
    std::function<void(uint64_t, const void *, uint32_t)> pci_dma_mem_wr_bytes_callback_;

    // Static instance pointers for callback wrappers.
    static constexpr uint32_t kMaxCallbackChipIds = 1024;
    static std::array<std::atomic<TTSimCommunicator *>, kMaxCallbackChipIds> callback_instances_by_chip_;
    static TTSimCommunicator *callback_instance_;

    // Static wrapper functions for C-style callbacks.
    static TTSimCommunicator *get_callback_instance(uint32_t chip_id);
    static void pci_dma_mem_rd_bytes_wrapper(uint64_t paddr, void *p, uint32_t size);
    static void pci_dma_mem_wr_bytes_wrapper(uint64_t paddr, const void *p, uint32_t size);
    static void pci_dma_mem_rd_bytes_by_chip_wrapper(uint32_t chip_id, uint64_t paddr, void *p, uint32_t size);
    static void pci_dma_mem_wr_bytes_by_chip_wrapper(
        uint32_t chip_id, uint64_t paddr, const void *p, uint32_t size);

    // Thread safety. In v3.5 shared-dlopen mode, libttsim_select_device_by_id()
    // and the following libttsim I/O call must be serialized across all
    // communicators because the active device selector is process-global.
    static std::mutex device_lock_;
};

}  // namespace tt::umd
