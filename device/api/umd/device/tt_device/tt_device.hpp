// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

// TODO: Should be moved to blackhole_architecture_implementation.h
// See /vendor_ip/synopsys/052021/bh_pcie_ctl_gen5/export/configuration/DWC_pcie_ctl.h.
static const uint64_t UNROLL_ATU_OFFSET_BAR = 0x1200;

// TODO: should be removed from tt_device.h, and put into blackhole_tt_device.h
// TODO: this is a bit of a hack... something to revisit when we formalize an
// abstraction for IO.
// BAR0 size for Blackhole, used to determine whether write block should use BAR0 or BAR4.
static const uint64_t BAR0_BH_SIZE = 512 * 1024 * 1024;

struct dynamic_tlb {
    uint64_t bar_offset;      // Offset that address is mapped to, within the PCI BAR.
    uint64_t remaining_size;  // Bytes remaining between bar_offset and end of the TLB.
};

class ArcMessenger;
class ArcTelemetryReader;
class RemoteCommunication;

class TTDevice {
public:
    /**
     * Creates a proper TTDevice object for the given device number.
     * Jtag support can be enabled.
     */
    static std::unique_ptr<TTDevice> create(int device_number, IODeviceType device_type = IODeviceType::PCIe);
    static std::unique_ptr<TTDevice> create(std::unique_ptr<RemoteCommunication> remote_communication);

    TTDevice(std::shared_ptr<PCIDevice> pci_device, std::unique_ptr<architecture_implementation> architecture_impl);
    TTDevice(
        std::shared_ptr<JtagDevice> jtag_device,
        uint8_t jlink_id,
        std::unique_ptr<architecture_implementation> architecture_impl);

    virtual ~TTDevice() = default;

    architecture_implementation *get_architecture_implementation();
    std::shared_ptr<PCIDevice> get_pci_device();
    std::shared_ptr<JtagDevice> get_jtag_device();

    tt::ARCH get_arch();

    const SocDescriptor &get_soc_descriptor() const;

    virtual void detect_hang_read(uint32_t data_read = HANG_READ_VALUE);
    virtual bool is_hardware_hung() = 0;

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

    // Read/write functions that always use same TLB entry. This is not supposed to be used
    // on any code path that is performance critical. It is used to read/write the data needed
    // to get the information to form cluster of chips, or just use base TTDevice functions.
    virtual void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);
    virtual void write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

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
    virtual void noc_multicast_write(void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr);

    /**
     * Read function that will send read message to the ARC core APB peripherals.
     *
     * @param mem_ptr pointer to memory which will receive the data
     * @param arc_addr_offset address offset in ARC core APB peripherals
     * @param size number of bytes
     *
     * NOTE: This function will read from APB peripherals. It will use the AXI interface to read the data if the chip is
     * local/PCIe, while the remote chip will use the NOC interface to read the data. Blackhole has board configurations
     * where the ARC is not available over AXI, hence in this situations, the NOC interface will be used even for local
     * chips.
     *
     * For additional details on the ARC core architecture and communication mechanisms, please refer to:
     * https://github.com/tenstorrent/tt-isa-documentation
     */
    virtual void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) = 0;

    /**
     * Write function that will send write message to the ARC core APB peripherals.
     *
     * @param mem_ptr pointer to memory from which the data is sent
     * @param arc_addr_offset address offset in ARC core APB peripherals
     * @param size number of bytes
     *
     * NOTE: This function will write to APB peripherals. It will use the AXI interface to write the data if the chip is
     * local/PCIe, while the remote chip will use the NOC interface to write the data. Blackhole has board
     * configurations where the ARC is not available over AXI, hence in this situations, the NOC
     * interface will be used even for local chips.
     *
     * For additional details on the ARC core architecture and communication mechanisms, please refer to:
     * https://github.com/tenstorrent/tt-isa-documentation
     */
    virtual void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) = 0;

    /**
     * Read function that will send read message to the ARC core CSM.
     *
     * @param mem_ptr pointer to memory which will receive the data
     * @param arc_addr_offset address offset in ARC core CSM
     * @param size number of bytes
     *
     * NOTE: This function will read from CSM. It will use the AXI interface to read the data if the chip is local/PCIe,
     * while the remote chip will use the NOC interface to read the data. Blackhole has board
     * configurations where the ARC is not available over AXI, hence in this situations, the NOC
     * interface will be used even for local chips.
     *
     * For additional details on the ARC core architecture and communication mechanisms, please refer to:
     * https://github.com/tenstorrent/tt-isa-documentation
     */
    virtual void read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) = 0;

    /**
     * Write function that will send write message to the ARC core CSM.
     *
     * @param mem_ptr pointer to memory from which the data is sent
     * @param arc_addr_offset address offset in ARC core CSM
     * @param size number of bytes
     *
     * NOTE: This function will write to CSM. It will use the AXI interface to write the data if the chip is local/PCIe,
     * while the remote chip will use the NOC interface to write the data. Blackhole has board
     * configurations where the ARC is not available over AXI, hence in this situations, the NOC
     * interface will be used even for local chips.
     *
     * For additional details on the ARC core architecture and communication mechanisms, please refer to:
     * https://github.com/tenstorrent/tt-isa-documentation
     */
    virtual void write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) = 0;

    void write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len);

    /**
     * Configures a PCIe Address Translation Unit (iATU) region.
     *
     * Device software expects to be able to access memory that is shared with
     * the host using the following NOC addresses at the PCIe core:
     * - GS: 0x0
     * - WH: 0x8_0000_0000
     * - BH: 0x1000_0000_0000_0000
     * Without iATU configuration, these map to host PA 0x0.
     *
     * While modern hardware supports IOMMU with flexible IOVA mapping, we must
     * maintain the iATU configuration to satisfy software that has hard-coded
     * the above NOC addresses rather than using driver-provided IOVAs.
     *
     * This interface is only intended to be used for configuring sysmem with
     * either 1GB hugepages or a compatible scheme.
     *
     * @param region iATU region index (0-15)
     * @param target DMA address (PA or IOVA) to map to
     * @param region_size size of the mapping window; must be (1 << 30)
     *
     * NOTE: Programming the iATU from userspace is architecturally incorrect:
     * - iATU should be managed by KMD to ensure proper cleanup on process exit
     * - Multiple processes can corrupt each other's iATU configurations
     * We should fix this!
     */
    virtual void configure_iatu_region(size_t region, uint64_t target, size_t region_size);

    virtual ChipInfo get_chip_info();

    semver_t get_firmware_version();

    /**
     * Waits for ARC core to be fully ready for communication.
     * Must be called before using ArcMessenger.
     * This ensures the ARC core is completely initialized and operational.
     */
    virtual bool wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) = 0;

    /**
     * Waits for ETH core training to complete.
     * @param eth_core Specific ETH core to wait on.
     * @param timeout_ms Timeout in ms.
     * @return Time taken in ms.
     */
    virtual std::chrono::milliseconds wait_eth_core_training(
        const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) = 0;

    void wait_dram_channel_training(
        const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms = timeout::DRAM_TRAINING_TIMEOUT);

    void bar_write32(uint32_t addr, uint32_t data);

    uint32_t bar_read32(uint32_t addr);

    ArcMessenger *get_arc_messenger() const;

    ArcTelemetryReader *get_arc_telemetry_reader() const;

    tt_xy_pair get_arc_core() const;

    FirmwareInfoProvider *get_firmware_info_provider() const;

    virtual RemoteCommunication *get_remote_communication() const { return nullptr; }

    virtual uint32_t get_clock() = 0;

    uint32_t get_max_clock_freq();

    virtual uint32_t get_min_clock_freq() = 0;

    uint64_t get_board_id();

    uint8_t get_asic_location();

    BoardType get_board_type();

    virtual bool get_noc_translation_enabled() = 0;

    double get_asic_temperature();

    virtual void wait_for_non_mmio_flush();

    bool is_remote();

    void init_tt_device(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT);

    uint64_t get_refclk_counter();

    int get_communication_device_id() const;

    IODeviceType get_communication_device_type() const;

    /**
     * Get the soft reset signal for the given riscs.
     *
     * @param core Core to get soft reset for, in translated coordinates
     */
    uint32_t get_risc_reset_state(tt_xy_pair core);

    /**
     * Set the soft reset signal for the given riscs.
     *
     * @param core Core to set soft reset for, in translated coordinates
     * @param risc_flags bitmask of riscs to set soft reset for
     */
    void set_risc_reset_state(tt_xy_pair core, const uint32_t risc_flags);

    virtual void dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr);

    virtual void dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr);

protected:
    std::shared_ptr<PCIDevice> pci_device_;
    std::shared_ptr<JtagDevice> jtag_device_;
    IODeviceType communication_device_type_ = IODeviceType::UNDEFINED;
    int communication_device_id_;
    std::unique_ptr<architecture_implementation> architecture_impl_;
    tt::ARCH arch;
    std::unique_ptr<ArcMessenger> arc_messenger_ = nullptr;
    LockManager lock_manager;
    std::unique_ptr<ArcTelemetryReader> telemetry = nullptr;
    std::unique_ptr<FirmwareInfoProvider> firmware_info_provider = nullptr;

    semver_t fw_version_from_telemetry(const uint32_t telemetry_data) const;

    TTDevice();
    TTDevice(std::unique_ptr<architecture_implementation> architecture_impl);

    ChipInfo chip_info;

    bool is_remote_tt_device = false;

    tt_xy_pair arc_core;

    std::optional<SocDescriptor> soc_descriptor_ = std::nullopt;

private:
    virtual void pre_init_hook(){};

    virtual void post_init_hook(){};

    void probe_arc();

    TlbWindow *get_cached_tlb_window();

    TlbWindow *get_cached_pcie_dma_tlb_window(tlb_data config);

    std::unique_ptr<TlbWindow> cached_tlb_window = nullptr;

    std::unique_ptr<TlbWindow> cached_pcie_dma_tlb_window = nullptr;

    std::mutex tt_device_io_lock;

    std::mutex pcie_dma_lock;
};

}  // namespace tt::umd
