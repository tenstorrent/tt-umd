/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/pcie/pci_device.hpp"

namespace tt::umd {

class PcieInterface {
public:
    // virtual ~PcieInterface() = default;

    virtual PCIDevice *get_pci_device() = 0;

    virtual void dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) = 0;

    virtual void dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) = 0;

    /**
     * DMA multicast write function that writes data to multiple cores on the NOC grid. Similar to noc_multicast_write
     * but uses DMA for better performance. Multicast writes data to a grid of cores. Cores must be specified in the
     * translated coordinate system so that the write lands on the intended cores.
     *
     * @param src pointer to memory from which the data is sent
     * @param size number of bytes
     * @param core_start starting core coordinates (x,y) of the multicast write
     * @param core_end ending core coordinates (x,y) of the multicast write
     * @param addr address on the device where data will be written
     */
    virtual void dma_multicast_write(
        void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) = 0;

    /**
     * DMA transfer from device to host.
     *
     * @param dst destination buffer
     * @param src AXI address corresponding to inbound PCIe TLB window; src % 4 == 0
     * @param size number of bytes
     * @throws std::runtime_error if the DMA transfer fails
     */
    virtual void dma_d2h(void *dst, uint32_t src, size_t size) = 0;

    /**
     * DMA transfer from device to host.
     *
     * @param dst destination buffer
     * @param src AXI address corresponding to inbound PCIe TLB window; src % 4 == 0
     * @param size number of bytes
     * @throws std::runtime_error if the DMA transfer fails
     */
    virtual void dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) = 0;

    /**
     * DMA transfer from host to device.
     *
     * @param dst AXI address corresponding to inbound PCIe TLB window; dst % 4 == 0
     * @param src source buffer
     * @param size number of bytes
     * @throws std::runtime_error if the DMA transfer fails
     */
    virtual void dma_h2d(uint32_t dst, const void *src, size_t size) = 0;

    /**
     * DMA transfer from host to device.
     *
     * @param dst AXI address corresponding to inbound PCIe TLB window; dst % 4 == 0
     * @param src source buffer
     * @param size number of bytes
     * @throws std::runtime_error if the DMA transfer fails
     */
    virtual void dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) = 0;

    /**
     * NOC multicast write function that will write data to multiple cores on NOC grid. Multicast writes data to a grid
     * of cores. Ideally cores should be in translated coordinate system. Putting cores in translated coordinate systems
     * will ensure that the write will land on the correct cores.
     *
     * @param dst pointer to memory from which the data is sent
     * @param size number of bytes
     * @param core_start starting core coordinates (x,y) of the multicast write
     * @param core_end ending core coordinates (x,y) of the multicast write
     * @param addr address on the device where data will be written
     */
    virtual void noc_multicast_write(
        void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) = 0;

    virtual void write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) = 0;

    virtual void bar_write32(uint32_t addr, uint32_t data) = 0;

    virtual uint32_t bar_read32(uint32_t addr) = 0;
};

}  // namespace tt::umd
