// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/tt_kmd_lib/tt_kmd_lib.h"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {
class semver_t;

struct PciDeviceInfo {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint16_t pci_domain;
    uint16_t pci_bus;
    uint16_t pci_device;
    uint16_t pci_function;
    std::string pci_bdf;
    // Physical slot is not always available on the system.
    // It is added to PciDeviceInfo struct in order for tt-metal to be able to use it
    // for machine provisioning tool at the moment, it is not explicitly used by UMD.
    // TODO: We should think about proper place for this field to live, probably some of the higher layers.
    std::optional<int> physical_slot;

    tt::ARCH get_arch() const;
    // TODO: does it make sense to move attributes that we can read from sysfs
    // onto this struct as methods?  e.g. current_link_width etc.
};

struct DmaBuffer {
    uint8_t *buffer = nullptr;
    uint8_t *completion = nullptr;
    size_t size = 0;

    uint64_t buffer_pa = 0;
    uint64_t completion_pa = 0;
};
/**
 * @brief Specifies the type of reset action for a Tenstorrent device.
 */
enum class TenstorrentResetDevice : uint32_t {
    /**
     * @brief Restores a device's saved configuration state after a reset.
     *
     * Used to write back previously saved configuration registers to return the
     * device to an operational state.
     */
    RESTORE_STATE = 0,

    /**
     * @brief Initiates a full PCIe link retraining (Hot Reset).
     *
     * A complete device reset that forces the PCIe link to re-establish its connection.
     */
    RESET_PCIE_LINK = 1,

    /**
     * @brief Triggers a software-initiated interrupt via a configuration write.
     *
     * Commands the device to generate an immediate interrupt by writing to a
     * control register.
     */
    CONFIG_WRITE = 2,

    /**
     * @brief Initiates a user-triggered device reset.
     *
     * Performs a reset operation initiated by user-level software to restore
     * the device to a known state.
     */
    USER_RESET = 3,

    /**
     * @brief Performs a complete ASIC reset.
     *
     * Resets the entire ASIC chip, restoring all internal logic and state
     * machines to their default state.
     */
    ASIC_RESET = 4,

    /**
     * @brief Resets the ASIC's DMC
     *
     * Specifically targets the device management controller.
     */
    ASIC_DMC_RESET = 5,

    /**
     * @brief Executes post-reset initialization procedures.
     *
     * Performs necessary cleanup and initialization tasks that must occur
     * after a device reset has completed.
     */
    POST_RESET = 6,
};

class PCIDevice {
    const std::string device_path;   // Path to character device: /dev/tenstorrent/N
    const int pci_device_num;        // N in /dev/tenstorrent/N
    const int pci_device_file_desc;  // Character device file descriptor
    const PciDeviceInfo info;        // PCI device info
    const int numa_node;             // -1 if non-NUMA
    const int revision;              // PCI revision value from sysfs
    const tt::ARCH arch;             // e.g. Wormhole, Blackhole
    const semver_t kmd_version;      // KMD version
    const bool iommu_enabled;        // Whether the system is protected from this device by an IOMMU
    DmaBuffer dma_buffer{};

public:
    /**
     * @return a list of integers corresponding to character devices in /dev/tenstorrent/
     */
    static std::vector<int> enumerate_devices(const std::unordered_set<int> &pci_target_devices = {});

    /**
     * @return a map of PCI device numbers (/dev/tenstorrent/N) to PciDeviceInfo
     */
    static std::map<int, PciDeviceInfo> enumerate_devices_info(const std::unordered_set<int> &pci_target_devices = {});

    /**
     * Read device information from sysfs.
     * @param fd Integer corresponding to the character device descriptor of the PCI device.
     * @return PciDeviceInfo struct containing the device information.
     */
    static PciDeviceInfo read_device_info(int fd);

    /**
     * PCI device constructor.
     *
     * Opens the character device file descriptor, reads device information from
     * sysfs, and maps device memory region(s) into the process address space.
     *
     * @param pci_device_number     N in /dev/tenstorrent/N
     */
    PCIDevice(int pci_device_number);

    /**
     * PCIDevice destructor.
     * Unmaps device memory and closes chardev file descriptor.
     */
    ~PCIDevice();

    PCIDevice(const PCIDevice &) = delete;       // copy
    void operator=(const PCIDevice &) = delete;  // copy assignment

    /**
     * @return PCI device info
     */
    const PciDeviceInfo get_device_info() const { return info; }

    /**
     * @return which NUMA node this device is associated with, or -1 if non-NUMA
     */
    int get_numa_node() const { return numa_node; }

    /**
     * @return N in /dev/tenstorrent/N
     * TODO: target for removal; upper layers should not care about this.
     */
    int get_device_num() const { return pci_device_num; }

    /**
     * @return PCI device id
     */
    int get_pci_device_id() const { return info.device_id; }

    /**
     * @return PCI revision value from sysfs.
     * TODO: target for removal; upper layers should not care about this.
     */
    int get_pci_revision() const { return revision; }

    /**
     * @return what architecture this device is (e.g. Wormhole, Blackhole, etc.)
     */
    tt::ARCH get_arch() const { return arch; }

    /**
     * @return whether the system is protected from this device by an IOMMU
     */
    bool is_iommu_enabled() const { return iommu_enabled; }

    /**
     * Map a buffer for hugepage access.
     *
     * @param buffer must be page-aligned
     * @param size must be a multiple of the page size
     * @return uint64_t Physical Address of hugepage.
     */
    uint64_t map_for_hugepage(void *buffer, size_t size);

    /**
     * Map a buffer so it is accessible by the device NOC.
     * @param buffer must be page-aligned
     * @param size must be a multiple of the page size
     * @return uint64_t NOC address, uint64_t PA or IOVA
     */
    std::pair<uint64_t, uint64_t> map_buffer_to_noc(void *buffer, size_t size);

    /**
     * Map a hugepage so it is accessible by the device NOC.
     * @param hugepage 1G hugepage
     * @param size in bytes (OK to be smaller than the hugepage size)
     * @return uint64_t NOC address, uint64_t PA or IOVA
     */
    std::pair<uint64_t, uint64_t> map_hugepage_to_noc(void *hugepage, size_t size);

    /**
     * Map a buffer for DMA access by the device.
     *
     * Supports mapping physically-contiguous buffers (e.g. hugepages) for the
     * no-IOMMU case.
     *
     * @param buffer must be page-aligned
     * @param size must be a multiple of the page size
     * @return uint64_t PA (no IOMMU) or IOVA (with IOMMU) for use by the device
     */
    uint64_t map_for_dma(void *buffer, size_t size);

    /**
     * Access the device's DMA buffer.  This buffer is not guaranteed to exist.
     * It is the caller's responsibility to check if the buffer is valid and to
     * chunk the desired transfer size to fit within it.
     */
    DmaBuffer &get_dma_buffer() { return dma_buffer; }

    /**
     * Unmap a buffer that was previously mapped for DMA access.
     *
     * @param buffer must be page-aligned
     * @param size must be a multiple of the page size
     */
    void unmap_for_dma(void *buffer, size_t size);

    /**
     * Read KMD version installed on the system.
     */
    static semver_t read_kmd_version();

    /**
     * Allocate TLB resource from KMD.
     *
     * @param tlb_size Size of the TLB caller wants to allocate.
     * @param mapping_type Type of TLB mapping to allocate (UC or WC).
     */
    std::unique_ptr<TlbHandle> allocate_tlb(const size_t tlb_size, const TlbMapping tlb_mapping = TlbMapping::UC);

    /**
     * Read command byte.
     */
    static uint8_t read_command_byte(const int pci_device_num);

    /**
     * Reset device via ioctl.
     */
    static void reset_device_ioctl(const std::unordered_set<int> &pci_target_devices, TenstorrentResetDevice flag);

    /**
     * Temporary function which allows us to support both ways of mapping buffers during the transition period.
     */
    static bool is_mapping_buffer_to_noc_supported();

    /**
     * Get the architecture of the PCIe device driver. The function enumerates PCIe devices on the system
     * and returns the architecture of the first device it finds. If no devices are found, returns Invalid architecture.
     * It also caches the value so subsequent calls are faster.
     */
    static tt::ARCH get_pcie_arch();

    /**
     * Checks if architecture-agnostic reset is supported by the device by checking the KMD version which enables this
     * feature.
     */
    static bool is_arch_agnostic_reset_supported();

public:
    // BAR0 base. UMD maps only ARC memory to user space, TLBs go through KMD.
    void *bar0 = nullptr;
    // We only map 3MB of BAR0, which covers NOC2AXI access and ARC CSM memory.
    static constexpr size_t bar0_size = 3 * (1 << 20);

    void *bar2_uc = nullptr;
    size_t bar2_uc_size;

    uint32_t read_checking_offset;

private:
    /**
     * Function will allocate PCIe DMA buffer that UMD uses for PCIe DMA transfers. To make the process of allocation
     * robust, allocation tries to allocate larger DMA buffers first and then shrinks the size until it reaches the
     * minimum size of single page. The idea behind this is that in of IOMMU being turned on, bigger buffers could be
     * allocated. In theory, bigger buffers should mean less DMA transfers and less overhead when performing PCIe DMA
     * operations.
     */
    void allocate_pcie_dma_buffer();

    /**
     * Tries to allocate a PCIe DMA buffer of the specified size when IOMMU is enabled on the system.
     * Uses PIN_PAGES IOCTL since ALLOCATE_DMA_BUF IOCTL has the upper limit on memory KMD can allocate for DMA
     * transactions.
     */
    bool try_allocate_pcie_dma_buffer_iommu(const size_t dma_buf_size);

    /**
     * Tries to allocate a PCIe DMA buffer of the specified size when IOMMU is not enabled on the system.
     * Uses ALLOCATE_DMA_BUF IOCTL which allocates physically contiguous memory for DMA transactions.
     */
    bool try_allocate_pcie_dma_buffer_no_iommu(const size_t dma_buf_size);

    static constexpr size_t bar0_mapping_offset = 509 * (1 << 20);

    tt_device_t *tt_device_handle = nullptr;
};

}  // namespace tt::umd
