/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "umd/device/types/host_memory.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class TlbWindow;

/**
 * @brief PCIe-specific device access: BAR register I/O and NUMA topology.
 *
 * Exposes operations that are only meaningful for PCIe-connected devices.
 * Available from TTDevice::get_pcie_interface() when the active transport is PCIe.
 */
class PcieInterface {
public:
    virtual ~PcieInterface() = default;

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

    /**
     * @brief Returns the NUMA node associated with this PCIe device.
     * @return int NUMA node ID, or -1 if the system is non-NUMA.
     */
    virtual int get_numa_node() const = 0;

    /**
     * @brief Exports (core, addr) as a dma-buf fd for peer-to-peer PCIe DMA, backed by a dedicated
     * TLB window.
     *
     * Temporary: this is not part of the Base API spec. It lives here only until the spec covers
     * dma-buf export; move it at that point.
     *
     * @param core Core to target.
     * @param addr Address within the core to aim the exported region at; must be page-aligned.
     * @param size Number of bytes to export; must be page-aligned and non-zero.
     * @param ordering Ordering mode for the TLB window backing the export.
     * @param noc_id NOC to route the exported traffic over.
     * @return int The dma-buf fd; the caller owns it and must close() it.
     */
    virtual int export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) = 0;

    /**
     * @brief Registers the callback consulted on an IO-op timeout to distinguish a hung NOC from a
     * slow one.
     *
     * Temporary: this is an implementation detail rather than
     * something the Base API spec should own. It lives here only until we align api
     * with TTDeviceModel; move/remove it at that point.
     *
     * @param hang_check Callback invoked with the NOC id of the in-flight op; an empty callback
     * disables hang detection.
     */
    virtual void set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) = 0;

    /**
     * @brief Requests or releases full hardware power domains via the KMD power API.
     *
     * Temporary: the Base API spec models power state as a DeviceFirmware responsibility
     * (firmware/ARC-driven), not a PcieInterface one. This KMD-ioctl-based implementation lives
     * here only until TTDeviceModel provides a real DeviceFirmware; move to that at that point.
     *
     * @param busy true to request full power, false to release power flags.
     */
    virtual void set_power_state(bool busy) = 0;

    /**
     * @brief Returns whether the system is protected from this device by an IOMMU.
     *
     * Temporary: this is an implementation detail rather than something the Base API spec should
     * own. Likely to disappear once system-memory allocation hides IOMMU detection internally;
     * move/remove it at that point.
     *
     * @return bool true if an IOMMU is present and protecting this device.
     */
    virtual bool is_iommu_enabled() const = 0;

    /**
     * @brief Writes a 32-bit value to a BAR2-relative device address.
     *
     * Temporary: BAR2 access is Blackhole-specific iATU configuration; the Base API spec only
     * models BAR0 register I/O (see bar_write32/bar_read32). Lives here only until iATU
     * programming is handled elsewhere; move/remove it at that point.
     *
     * @param addr BAR2-relative address.
     * @param data The 32-bit value to write.
     */
    virtual void bar2_write32(uint32_t addr, uint32_t data) = 0;

    /**
     * @brief Returns whether BAR2 is mapped and usable.
     *
     * Temporary: see bar2_write32.
     *
     * @return bool true if BAR2 is mapped.
     */
    virtual bool is_bar2_available() const = 0;

    /**
     * @brief Returns this device's PCI Bus:Device.Function address string.
     *
     * Temporary: PCI topology info (BDF, bus number) isn't part of the Base API spec, which
     * models devices by communication_device_id rather than raw PCI addressing. Lives here only
     * until cluster topology reporting is aligned; move/remove it at that point.
     *
     * @return std::string The PCI BDF string (e.g. "0000:01:00.0").
     */
    virtual std::string get_pci_bdf() const = 0;

    /**
     * @brief Returns this device's PCI bus number.
     *
     * Temporary: see get_pci_bdf.
     *
     * @return uint16_t The PCI bus number.
     */
    virtual uint16_t get_pci_bus() const = 0;

    /**
     * @brief Returns the PCI revision value read from sysfs.
     *
     * Temporary: see get_pci_bdf. Used for host-memory-channel capacity lookup tables keyed by
     * device/revision ID.
     *
     * @return int The PCI revision value.
     */
    virtual int get_pci_revision() const = 0;

    /**
     * @brief Returns whether the newer KMD mapping scheme (mapping host buffers directly to NOC
     * addresses) is supported, as opposed to the legacy iATU-based scheme.
     *
     * Temporary: sysmem/DMA buffer mapping isn't part of the Base API spec, which models this via
     * SystemMemoryAllocator/SystemMemoryBuffer instead. Lives here only until that abstraction is
     * implemented; move/remove it at that point.
     *
     * @return bool true if buffers can be mapped directly to NOC addresses.
     */
    virtual bool is_mapping_buffer_to_noc_supported() const = 0;

    /**
     * @brief Returns whether host pages can be pinned such that the device may read but not write
     * them.
     *
     * Temporary: see is_mapping_buffer_to_noc_supported.
     *
     * @return bool true if read-only page pinning is supported.
     */
    virtual bool is_read_only_page_pinning_supported() const = 0;

    /**
     * @brief Maps a buffer for DMA access by the device, returning a NOC address in addition to
     * the DMA address.
     *
     * Temporary: see is_mapping_buffer_to_noc_supported.
     *
     * @param buffer Must be page-aligned.
     * @param size Must be a multiple of the page size.
     * @param device_access Whether the device may read, write, or both.
     * @return std::pair<uint64_t, uint64_t> NOC address, PA or IOVA.
     */
    virtual std::pair<uint64_t, uint64_t> map_buffer_to_noc(
        void* buffer, size_t size, DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) = 0;

    /**
     * @brief Maps a hugepage so it is accessible by the device NOC.
     *
     * Temporary: see is_mapping_buffer_to_noc_supported.
     *
     * @param hugepage 1G hugepage.
     * @param size In bytes (may be smaller than the hugepage size).
     * @return std::pair<uint64_t, uint64_t> NOC address, PA or IOVA.
     */
    virtual std::pair<uint64_t, uint64_t> map_hugepage_to_noc(void* hugepage, size_t size) = 0;

    /**
     * @brief Maps a buffer for DMA access by the device, without the newer KMD's NOC-address
     * mapping.
     *
     * Temporary: see is_mapping_buffer_to_noc_supported.
     *
     * @param buffer Must be page-aligned.
     * @param size Must be a multiple of the page size.
     * @param device_access Whether the device may read, write, or both.
     * @return uint64_t PA (no IOMMU) or IOVA (with IOMMU) for use by the device.
     */
    virtual uint64_t map_for_dma(
        void* buffer, size_t size, DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) = 0;

    /**
     * @brief Maps a hugepage for DMA access by the device (legacy iATU-based scheme).
     *
     * Temporary: see is_mapping_buffer_to_noc_supported.
     *
     * @param buffer Must be page-aligned.
     * @param size Must be a multiple of the page size.
     * @return uint64_t Physical Address of the hugepage.
     */
    virtual uint64_t map_for_hugepage(void* buffer, size_t size) = 0;

    /**
     * @brief Unmaps a buffer that was previously mapped for DMA access.
     *
     * Temporary: see is_mapping_buffer_to_noc_supported.
     *
     * @param buffer Must be page-aligned.
     * @param size Must be a multiple of the page size.
     */
    virtual void unmap_for_dma(void* buffer, size_t size) = 0;

    /**
     * @brief Allocates a TLB-backed I/O window.
     *
     * Temporary: TLB allocation isn't part of the Base API spec, which models this via IoWindow/
     * TTDeviceModel::create_io_window() instead. Lives here only until that abstraction is
     * implemented; move/remove it at that point.
     *
     * @param config Device-side target configuration applied to the new window.
     * @param mapping UC or WC.
     * @param size Requested TLB size in bytes.
     * @return std::unique_ptr<TlbWindow> The allocated window.
     */
    virtual std::unique_ptr<TlbWindow> allocate_tlb_window(tlb_data config, TlbMapping mapping, size_t size) = 0;
};

}  // namespace tt::umd
