/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/dma_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_dma/dma_transfer.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {

class PCIDevice;
class TlbWindow;
struct tlb_data;
enum class WindowFlags : uint32_t;

/**
 * PcieProtocol implements DeviceProtocol, PcieInterface, and DmaInterface for PCIe-connected
 * devices.
 *
 * Provides PCIe-based device I/O including DMA transfers, register access,
 * and multicast writes.
 */
class PcieProtocol : public DeviceProtocol, public PcieInterface, public DmaInterface {
public:
    explicit PcieProtocol(std::unique_ptr<PCIDevice> pci_device, bool use_safe_api = false);

    ~PcieProtocol() override;

    // DeviceProtocol interface.
    void write_data(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_data(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_ctrl(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    bool write_to_core_range(
        const void* mem_ptr, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id)
        override;
    int get_mmio_id() override;

    // PcieInterface.
    void set_power_state(PowerState state) override;
    void bar_write32(uint32_t addr, uint32_t data) override;
    uint32_t bar_read32(uint32_t addr) override;
    int get_numa_node() const override;
    int export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) override;
    void set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) override;

    // DmaInterface.
    [[nodiscard]] bool dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_multicast_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    bool dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    bool dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    bool dma_multicast_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    [[nodiscard]] DmaState dma_read_zero_copy_start(
        uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] DmaState dma_read_zero_copy_check() override;
    [[nodiscard]] DmaState dma_write_zero_copy_start(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] DmaState dma_write_zero_copy_check() override;

    // Not part of any Base API interface: internal PCIe plumbing reached by TTDevice via the
    // concrete PcieProtocol rather than through PcieInterface/DmaInterface.
    // TODO: delete this along with TTDevice::get_pci_device() once callers go through PcieInterface only.
    PCIDevice* get_pci_device();

private:
    TlbWindow* get_cached_tlb_window();
    // Allocates the DMA window on first use and hands it back. Aiming it at an address is
    // target_dma_window()'s job, so a caller that is about to do that does not configure twice.
    TlbWindow* get_dma_tlb_window(const tlb_data& config);

    // Points the DMA window at addr and returns the AXI address the engine reaches it through. The
    // window covers a size-aligned span, so that address is the window's base plus addr's offset
    // into the span.
    uint64_t target_dma_window(TlbWindow& tlb_window, tlb_data& config, uint64_t addr);

    static DmaTransferStrategy create_dma_strategy(tt::ARCH arch);
    static size_t get_dma_tlb_size(tt::ARCH arch);

    void noc_multicast_write(
        const void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id);

    enum class DmaDirection { H2D, D2H };
    tlb_data create_dma_tlb_config(
        uint64_t addr,
        tt_xy_pair core_end,
        NocId noc_id,
        WindowFlags flags,
        std::optional<tt_xy_pair> core_start = std::nullopt);
    bool dma_transfer(void* buffer, size_t size, uint64_t addr, tlb_data config, DmaDirection direction);
    bool dma_transfer_zero_copy(uint64_t iova, size_t size, uint64_t addr, tlb_data config, DmaDirection direction);

    // Takes the PCIE_DMA lock, which is what keeps other processes off this device's one DMA engine.
    // acquire_dma_channel() waits for it; try_acquire_dma_channel() hands back a lock owning nothing
    // when it is taken, which the caller checks with owns_lock().
    // Callers hold dma_mutex_ and have already found in_flight_dma_ empty. Taking the two in that
    // order everywhere is what keeps a blocking transfer from waiting on a channel this process
    // holds for an asynchronous one.
    std::unique_lock<MutexInterface> acquire_dma_channel();
    std::unique_lock<MutexInterface> try_acquire_dma_channel();

    // Programs one descriptor and rings the doorbell. Callers hold dma_mutex_.
    void dma_start(uint64_t host_addr, uint32_t axi_address, size_t size, DmaDirection direction);
    // Reads back whether the transfer dma_start() programmed has finished. Callers hold dma_mutex_.
    bool dma_is_complete(DmaDirection direction);
    // dma_start() followed by polling dma_is_complete(), throwing on timeout. Callers hold dma_mutex_.
    void dma_transfer_and_wait(uint64_t host_addr, uint32_t axi_address, size_t size, DmaDirection direction);

    // Shared halves of the asynchronous transfers. Both take dma_mutex_ themselves.
    DmaState dma_zero_copy_start(uint64_t iova, size_t size, uint64_t addr, tlb_data config, DmaDirection direction);
    DmaState dma_zero_copy_check(DmaDirection direction);

    // Validates the addresses and size a transfer is about to be programmed with, throwing on
    // anything the DMA engine cannot do.
    void validate_dma_transfer(uint64_t host_addr, uint32_t axi_address, size_t size, DmaDirection direction);

    // The transfer an asynchronous start left running, until its check reports it done. Also what
    // makes a second start, or a blocking transfer, refuse to touch the engine meanwhile.
    struct InFlightDma {
        DmaDirection direction;
        std::chrono::steady_clock::time_point deadline;
    };

    // Offset used to access NOC2AXI config + ARC specific memory (ICCM + CSM + APB).
    static constexpr uint32_t BAR0_OFFSET = 0x1FD00000;

    std::unique_ptr<PCIDevice> pci_device_;
    DmaTransferStrategy dma_strategy_;
    bool use_safe_api_;
    std::mutex io_lock_;
    // Guards the DMA engine registers and the DMA TLB window against threads of this process. Other
    // processes are kept out by the PCIE_DMA lock, which this is always taken before.
    std::mutex dma_mutex_;
    std::optional<InFlightDma> in_flight_dma_;
    // The PCIE_DMA lock while an asynchronous transfer is in flight, from its start until the check
    // that reports it done. A blocking transfer takes and releases the same lock within one call and
    // leaves this alone. Released on destruction if a caller never checked, which does not stop
    // hardware: a transfer still running then keeps writing where it was told to.
    std::unique_lock<MutexInterface> dma_channel_lock_;
    std::unique_ptr<TlbWindow> cached_tlb_window_;
    std::unique_ptr<TlbWindow> cached_dma_tlb_window_;

    // Hang check consulted on an IO-op timeout; empty until a HangDetector is wired in (see
    // TTDevice::wire_hang_detector).
    std::function<bool(NocId)> hang_check_;
};

}  // namespace tt::umd
