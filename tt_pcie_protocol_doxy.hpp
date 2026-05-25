// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace tt::umd {

/**
 * @defgroup tt_pcie_protocol PcieInterface and PcieProtocol
 * @{
 *
 * @brief PCIe transport: DMA transfers, BAR register access, and NOC multicast.
 *
 * PcieInterface defines PCIe-specific operations beyond the base @ref DeviceProtocol.
 * PcieProtocol implements both @ref DeviceProtocol and PcieInterface for
 * PCIe-connected devices.
 *
 * All core coordinates at this layer are tt_xy_pair — a raw (x, y) pair
 * used directly in the I/O transaction with no coordinate translation.
 * The @ref TTDevice layer translates CoreCoord to tt_xy_pair before delegating here.
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref DeviceProtocol | Base I/O interface that PcieProtocol implements |
 * | @ref PcieInterface | PCIe-specific I/O interface that PcieProtocol implements |
 * | @ref PCIDevice | Underlying PCIe device handle |
 *
 */

class PcieInterface {
public:
    virtual ~PcieInterface() = default;

    /** @name DMA Transfers (Bounce Buffer) */
    /** @{ */

    /**
     * @brief Executes a Host-to-Device DMA transfer using an internal bounce buffer.
     *
     * Copies from the caller-provided buffer into an internal pinned staging buffer
     * before issuing the hardware DMA to the device.
     *
     * @param src Pointer to the source host memory.
     * @param dst_addr Destination address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Target core coordinates.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    [[nodiscard]] virtual bool dma_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Executes a Device-to-Host DMA transfer using an internal bounce buffer.
     *
     * DMAs data into an internal pinned staging buffer and then copies it into the
     * caller-provided buffer.
     *
     * @param dst Pointer to the destination host buffer.
     * @param src_addr Source address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinates.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    [[nodiscard]] virtual bool dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Executes a multicast Host-to-Device DMA transfer using an internal bounce buffer.
     *
     * Broadcasts data to a rectangular grid of cores via the internal staging buffer.
     *
     * @param src Pointer to the source host memory.
     * @param dst_addr Destination address on the target cores.
     * @param size Number of bytes to transfer.
     * @param core_start Top-left core of the multicast rectangle.
     * @param core_end Bottom-right core of the multicast rectangle.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    [[nodiscard]] virtual bool dma_multicast_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) = 0;

    /** @} */

    /** @name DMA Transfers (Zero-Copy) */
    /** @{ */

    /**
     * @brief Executes a zero-copy Host-to-Device DMA transfer.
     *
     * Operates directly on caller-managed pinned host memory identified by its IOVA,
     * bypassing the internal staging buffer.
     *
     * @param src_iova IOVA of the source pinned host memory buffer.
     * @param dst_addr Destination address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Target core coordinates.
     * @param noc_id NOC to route through.
     */
    virtual void dma_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Executes a zero-copy Device-to-Host DMA transfer.
     *
     * Operates directly on caller-managed pinned host memory identified by its IOVA,
     * bypassing the internal staging buffer.
     *
     * @param dst_iova IOVA of the destination pinned host memory buffer.
     * @param src_addr Source address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinates.
     * @param noc_id NOC to route through.
     */
    virtual void dma_read_zero_copy(
        uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Executes a zero-copy multicast Host-to-Device DMA transfer.
     *
     * Broadcasts data to a rectangular grid of cores directly from caller-managed
     * pinned host memory, bypassing the internal staging buffer.
     *
     * @param src_iova IOVA of the source pinned host memory buffer.
     * @param dst_addr Destination address on the target cores.
     * @param size Number of bytes to transfer.
     * @param core_start Top-left core of the multicast rectangle.
     * @param core_end Bottom-right core of the multicast rectangle.
     * @param noc_id NOC to route through.
     */
    virtual void dma_multicast_write_zero_copy(
        uint64_t src_iova,
        uint64_t dst_addr,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        NocId noc_id) = 0;

    /** @} */

    /** @name NOC Multicast */
    /** @{ */

    /**
     * @brief Writes data to a range of cores via TLB-mapped MMIO multicast.
     *
     * Unlike dma_multicast_write(), this uses TLB-mapped MMIO rather than DMA
     * for the host-to-device transfer.
     *
     * @param src Pointer to the source host memory.
     * @param size Number of bytes to write.
     * @param core_start First core in the multicast rectangle.
     * @param core_end Last core in the multicast rectangle.
     * @param addr Destination address on each target core.
     * @param noc_id NOC to route through.
     */
    virtual void noc_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) = 0;

    /** @} */

    /** @name BAR Register Access */
    /** @{ */

    /**
     * @brief Writes a 32-bit value to a BAR-relative device address.
     * @param addr BAR-relative address.
     * @param data The 32-bit value to write.
     */
    virtual void bar_write32(uint32_t addr, uint32_t data) = 0;

    /**
     * @brief Reads a 32-bit value from a BAR-relative device address.
     * @param addr BAR-relative address.
     * @return uint32_t The value read.
     */
    virtual uint32_t bar_read32(uint32_t addr) = 0;

    /** @} */

    /**
     * @brief Returns the NUMA node associated with this PCIe device.
     * @return int NUMA node ID, or -1 if the system is non-NUMA.
     */
    virtual int get_numa_node() const = 0;

    /**
     * @brief Returns a pointer to the underlying @ref PCIDevice.
     * @return PCIDevice* pointer to the PCIe device handle.
     */
    virtual PCIDevice* get_pci_device() = 0;
};

/**
 * @brief PCIe transport implementation of @ref DeviceProtocol and PcieInterface.
 *
 * Provides PCIe-based device I/O including TLB-mapped reads/writes, DMA
 * transfers, BAR register access, and NOC multicast writes.
 */
class PcieProtocol : public DeviceProtocol, public PcieInterface {
public:
    /**
     * @brief Constructs a PcieProtocol that takes ownership of a @ref PCIDevice.
     * @param pci_device The PCIe device to operate on.
     * @param use_safe_api If true, uses ordered (non-WC) writes for all I/O paths.
     */
    explicit PcieProtocol(std::unique_ptr<PCIDevice> pci_device, bool use_safe_api = false);

    ~PcieProtocol() override;

    /** @name DeviceProtocol Overrides */
    /** @{ */
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint32_t size, NocId noc_id)
        override;
    int get_mmio_id() override;
    /** @} */

    /** @name PcieInterface Overrides */
    /** @{ */
    int get_numa_node() const override;
    PCIDevice* get_pci_device() override;
    [[nodiscard]] bool dma_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_multicast_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    void dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    void dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    void dma_multicast_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    void noc_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) override;
    void bar_write32(uint32_t addr, uint32_t data) override;
    uint32_t bar_read32(uint32_t addr) override;
    /** @} */

private:
    /// Owned PCIe device handle.
    std::unique_ptr<PCIDevice> pci_device_;
};

/** @} */  // end of tt_pcie_protocol group

}  // namespace tt::umd
