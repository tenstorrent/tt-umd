// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "tt_device_error.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/firmware_telemetry_reader.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/architecture_registers.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/tt_device_model/tt_device_model.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/eth_training_status.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class ArcTelemetryReader;
class DeviceFirmware;
class RemoteCommunication;
class SimulationSysmemManager;
class DmaInterface;
class JtagDevice;
class JtagInterface;
class PCIDevice;
class PcieInterface;
class PcieProtocol;
class RemoteInterface;
class TLBManager;
enum class NocId : uint8_t;
enum class RiscType : std::uint64_t;
struct CoreCoord;

class TTDevice {
public:
    /**
     * @brief Factory method to create a TTDevice instance.
     *
     * Creates and returns a unique pointer to a TTDevice object configured with the
     * specified parameters. This is the primary way to instantiate TTDevice objects.
     *
     * @param device_number The device identifier/index to connect to, specific to the I/O device interface.
     * @param device_type The type of I/O device interface to use. (default: PCIe)
     * @param use_safe_api Flag to enable safe I/O API that can recover from SIGBUS errors.
     *                     Available only for PCIe I/O device type. (default: false)
     * @param soc_arch_descriptor Shared pointer to the SoC architecture descriptor.
     *                            If nullptr, a default descriptor will be used. (default: nullptr)
     *
     * @return std::unique_ptr<TTDevice> A unique pointer to the created TTDevice instance.
     *
     * @throws May throw exceptions if device creation fails or device_number is invalid.
     */
    static std::unique_ptr<TTDevice> create(
        int device_number,
        IODeviceType device_type = IODeviceType::PCIe,
        bool use_safe_api = false,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor = nullptr);

    static std::unique_ptr<TTDevice> create(
        std::unique_ptr<RemoteCommunication> remote_communication,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor = nullptr);

#ifdef TT_UMD_BUILD_SIMULATION
    // A remote TTDevice is normally initialized over ARC (init_tt_device), which constructs its SocDescriptor.
    // Simulated remote chips have no ARC to probe, so the caller supplies the full descriptor directly. This is a
    // dedicated factory (compiled in only for simulation builds) rather than an overload of the silicon create()
    // above, so the simulation-only flow stays fully separated from the silicon path.
    // TODO: temporary - remove once ttsim provides a mocked ARC that can serve the SocDescriptor like silicon does.
    static std::unique_ptr<TTDevice> create_simulation_remote(
        std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor &soc_descriptor);
#endif  // TT_UMD_BUILD_SIMULATION

    virtual ~TTDevice() = default;

    ArchitectureImplementation *get_architecture_implementation();
    PCIDevice *get_pci_device();
    RemoteCommunication *get_remote_communication();

    DeviceProtocol *get_device_protocol();
    PcieInterface *get_pcie_interface();
    JtagInterface *get_jtag_interface();
    RemoteInterface *get_remote_interface();

    tt::ARCH get_arch() const;

    /**
     * @brief Controls what happens when a hang is confirmed.
     */
    enum class HangAction {
        THROW,   ///< Throw an exception (depending on type of hang) (default).
        RETURN,  ///< Return instead of throwing.
    };

    /**
     * @brief Defines the requested power domain state for the device.
     */
    enum class PowerState {
        BUSY,  ///< Claims all power domains, requesting maximum performance.
        IDLE,  ///< Releases power domains, allowing the device to enter lower power states.
    };

    /**
     * Check if the PCIe communication is hung.
     *
     * Reads a known register over BAR and compares the result against the hang
     * signature. If the device is not locally accessible (e.g. JTAG or remote),
     * the check is skipped and false is returned.
     *
     * @param data_read  Value to compare against the hang signature. Defaults to
     *                   HANG_READ_VALUE so callers can simply invoke is_pcie_hung()
     *                   after any BAR read that returned a suspicious value.
     * @param action     What to do when a hang is confirmed. Defaults to Throw.
     * @return true if the PCIe communication appears hung (only reachable with ReturnValue).
     * @throws std::runtime_error if a confirmed hang is detected and action is Throw.
     */
    bool is_pcie_hung(uint32_t data_read = HANG_READ_VALUE, HangAction action = HangAction::THROW);

    /**
     * Check if NOC traffic to the device is hung.
     *
     * Sends a read over the specified NOC and compares the result against the
     * hang signature. Only meaningful for locally accessible devices; on remote
     * devices the check is skipped and false is returned.
     *
     * @param noc     NOC to check (NOC0 or NOC1).
     * @param action  What to do when a hang is confirmed. Defaults to Throw.
     * @return true if the NOC appears hung (only reachable with ReturnValue).
     * @throws std::runtime_error if a confirmed hang is detected and action is Throw.
     */
    bool is_noc_hung(NocId noc, HangAction action = HangAction::THROW);

    // Read/write functions that always use same TLB entry. This is not supposed to be used
    // on any code path that is performance critical. It is used to read/write the data needed
    // to get the information to form cluster of chips, or just use base TTDevice functions.
    virtual void read_from_device(
        void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id = NocId::DEFAULT_NOC);
    virtual void write_to_device(
        const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id = NocId::DEFAULT_NOC);

    virtual void read_from_device_reg(
        void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id = NocId::DEFAULT_NOC);
    virtual void write_to_device_reg(
        const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * @brief Executes a Host-to-Device (H2D) DMA transfer using an internal bounce buffer.
     *
     * Copies from the user-provided buffer into an internal pinned staging buffer
     * before issuing the hardware DMA to the device.
     *
     * @param src Pointer to the user-provided buffer containing the data to send.
     * @param dst_addr Destination address on the target device core.
     * @param size Number of bytes to transfer.
     * @param core Target core coordinate on the device.
     * @param noc_id Physical network to route the transaction over. Defaults to NocId::DEFAULT_NOC.
     */
    virtual void dma_write(
        const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * @brief Executes a Device-to-Host (D2H) DMA transfer using an internal bounce buffer.
     *
     * DMAs data into an internal pinned staging buffer and then copies it into the
     * user-provided buffer.
     *
     * @param dst Pointer to the user-provided buffer where data will be received.
     * @param src_addr Source address on the target device core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinate on the device.
     * @param noc_id Physical network to route the transaction over. Defaults to NocId::DEFAULT_NOC.
     */
    virtual void dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * @brief Executes a multicast Host-to-Device DMA transfer using an internal bounce buffer.
     *
     * Broadcasts data to a rectangular grid of cores via the internal staging buffer. Cores must be
     * specified in the translated coordinate system so that the write lands on the intended cores.
     *
     * @param src Pointer to the user-provided buffer containing the data to send.
     * @param dst_addr Destination address on the target device cores.
     * @param size Number of bytes to transfer.
     * @param core_start Top-left core coordinate of the multicast grid.
     * @param core_end Bottom-right core coordinate of the multicast grid.
     * @param noc_id Physical network to route the transaction over. Defaults to NocId::DEFAULT_NOC.
     */
    virtual void dma_write_to_core_range(
        const void *src,
        uint64_t dst_addr,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * @brief Executes a zero-copy Device-to-Host (D2H) DMA transfer.
     *
     * Operates directly on caller-managed pinned host memory identified by its IOVA, bypassing the
     * internal staging buffer. Unlike dma_read, there is no non-DMA fallback: this throws if DMA is
     * unavailable.
     *
     * @param dst_iova IOVA of the destination pinned host memory buffer.
     * @param src_addr Source address on the target device core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinate on the device.
     * @param noc_id Physical network to route the transaction over. Defaults to NocId::DEFAULT_NOC.
     */
    virtual void dma_read_zero_copy(
        uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * @brief Executes a zero-copy Host-to-Device (H2D) DMA transfer.
     *
     * Operates directly on caller-managed pinned host memory identified by its IOVA, bypassing the
     * internal staging buffer. Unlike dma_write, there is no non-DMA fallback: this throws if DMA is
     * unavailable.
     *
     * @param src_iova IOVA of the source pinned host memory buffer.
     * @param dst_addr Destination address on the target device core.
     * @param size Number of bytes to transfer.
     * @param core Target core coordinate on the device.
     * @param noc_id Physical network to route the transaction over. Defaults to NocId::DEFAULT_NOC.
     */
    virtual void dma_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * NOC multicast write function that will write data to multiple cores on NOC grid. Multicast writes data to a grid
     * of cores. Ideally cores should be in translated coordinate system. Putting cores in translated coordinate systems
     * will ensure that the write will land on the correct cores.
     *
     * @param src pointer to memory from which the data is sent
     * @param size number of bytes
     * @param core_start starting core coordinates (x,y) of the multicast write
     * @param core_end ending core coordinates (x,y) of the multicast write
     * @param addr address on the device where data will be written
     */
    virtual void noc_multicast_write(
        const void *src,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        uint64_t addr,
        NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * NOC multicast write function that will write data to all TENSIX cores in the grid.
     *
     * @param src pointer to memory from which the data is sent
     * @param size number of bytes
     * @param addr address on the device where data will be written
     */
    virtual void noc_multicast_write(const void *src, size_t size, uint64_t addr, NocId noc_id = NocId::DEFAULT_NOC);

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

    ChipInfo get_chip_info();

    FirmwareBundleVersion get_firmware_version();

    /**
     * Interface to the device's management firmware. Owned by the model, so never null; created
     * with the device, initialized by init_tt_device() through DeviceFirmware::init_firmware().
     */
    DeviceFirmware *get_device_firmware() const;

    /**
     * Waits for ETH core training to complete.
     * @param eth_core Specific ETH core to wait on.
     * @param timeout_ms Timeout in ms.
     * @return Time taken in ms.
     */
    std::chrono::milliseconds wait_eth_core_training(
        CoreCoord eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);

    void wait_dram_channel_training(
        const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms = timeout::DRAM_TRAINING_TIMEOUT);

    void bar_write32(uint32_t addr, uint32_t data);

    uint32_t bar_read32(uint32_t addr);

    FirmwareTelemetryReader *get_firmware_telemetry_reader() const;

    tt_xy_pair get_arc_core() const;

    tt_xy_pair get_arc_core(const NocId noc_id) const;

    FirmwareInfoProvider *get_firmware_info_provider() const;

    /**
     * @brief Requests a hardware power domain state change.
     *
     * Claims or releases full power domains. No-op for remote devices.
     *
     * @param state The requested power state (BUSY or IDLE).
     */
    virtual void set_power_state(PowerState state, NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * @brief Sets the device clock frequency.
     *
     * Controls the AICLK frequency the device runs at. Distinct from set_power_state(), which
     * manages hardware power domains.
     *
     * @param state The target clock state (BUSY = max frequency, IDLE = min frequency).
     * @param noc_id Currently ignored: the call routes on the thread-selected NOC, matching the
     * per-arch overrides this replaced. Honoring the parameter end-to-end is follow-up work.
     */
    void set_clock_state(ClockState state, NocId noc_id = NocId::DEFAULT_NOC);

    virtual uint32_t get_clock() = 0;

    uint32_t get_max_clock_freq();

    virtual uint32_t get_min_clock_freq() = 0;

    // Advance the device by one clock cycle. No-op by default; overridden by devices with a
    // controllable clock (e.g. simulation). Simulator clocking must be deterministic, so the
    // clock is advanced synchronously from the calling thread rather than driven by a
    // background thread.
    virtual void advance_device_execution();

    uint64_t get_board_id();

    uint8_t get_asic_location();

    BoardType get_board_type();

    bool get_noc_translation_enabled();

    double get_asic_temperature();

    virtual void wait_for_non_mmio_flush();

    bool is_remote();

    void init_tt_device(std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT);

    uint64_t get_refclk_counter();

    int get_communication_device_id() const;

    IODeviceType get_communication_device_type() const;

    /**
     * Get the soft reset signal for the given riscs.
     *
     * @param core Core to get soft reset for, in translated coordinates
     */
    uint32_t get_risc_reset_state(CoreCoord core);

    /**
     * Set the soft reset signal for the given riscs.
     *
     * @param core Core to set soft reset for, in translated coordinates
     * @param risc_flags bitmask of riscs to set soft reset for
     */
    void set_risc_reset_state(CoreCoord core, const uint32_t risc_flags);

    /**
     * Assert risc reset for a specific core.
     *
     * @param core Core to assert reset for, in translated coordinates
     * @param selected_riscs Bitmask of riscs to assert reset for
     */
    virtual void assert_risc_reset(CoreCoord core, const RiscType selected_riscs);

    /**
     * Deassert risc reset for a specific core.
     *
     * @param core Core to deassert reset for, in translated coordinates
     * @param selected_riscs Bitmask of riscs to deassert reset for
     * @param staggered_start Whether to use staggered start
     */
    virtual void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start);

    virtual SimulationSysmemManager *get_sysmem_manager() { return nullptr; }

    /**
     * Allocate a TlbWindow for use by callers (typically TLBManager).
     *
     * Default implementation uses PCIDevice::allocate_tlb (silicon path) and
     * wraps the resulting handle in a SiliconTlbWindow. Simulation TTDevice
     * subclasses override this to allocate from their in-process bitmap and
     * build the appropriate sim-backend TlbWindow.
     *
     * @param config tlb_data configuration applied to the new window.
     * @param mapping UC or WC.
     * @param size Requested TLB size in bytes (0 means try arch-supported sizes in order).
     */
    virtual std::unique_ptr<TlbWindow> get_io_window(
        tlb_data config, TlbMapping mapping = TlbMapping::WC, size_t size = 0);

    /**
     * @brief Creates an I/O window mapping a region of host virtual address space to device address space.
     *
     * The returned window supports direct pointer-style reads and writes to device memory.
     * It can be reconfigured at runtime to point to different device addresses.
     *
     * The window is created large enough to cover the requested size, rounded up to a size the
     * architecture provides; @ref IoWindow::get_size reports what was actually created. A requested
     * size of 0 leaves the choice to the implementation. Cores are named in the translated coordinate
     * system, and a target without a NOC is routed over the NOC selected for this thread. Naming a
     * second corner makes the window a multicast grid, which requires NOC translation.
     *
     * @param target Device-side target describing the core(s), address, optional NOC and flags.
     * See @ref TargetIoWindowConfig.
     * @param host Host-side properties (caching strategy and requested size).
     * See @ref HostIoWindowConfig.
     * @return An exclusively owned handle to the newly created @ref IoWindow.
     */
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host);

    /**
     * Export a NOC-addressable region as a dma-buf file descriptor for peer-to-peer PCIe DMA.
     * Requires a PCIe-attached device. See PcieInterface::export_dmabuf for the full contract; the
     * caller owns the returned fd and must close() it.
     *
     * @param core Core to target.
     * @param addr Address within the core to aim the exported region at; must be page-aligned.
     * @param size Number of bytes to export; must be page-aligned and non-zero.
     * @param ordering Ordering mode for the TLB window backing the export.
     * @param noc_id NOC to route the exported traffic over.
     */
    virtual int export_dmabuf(
        CoreCoord core,
        uint64_t addr,
        size_t size,
        uint64_t ordering = tlb_data::Relaxed,
        NocId noc_id = NocId::DEFAULT_NOC);

    static void set_sigbus_safe_handler(bool set_safe_handler);

    /**
     * Read the training status of the given ETH core.
     *
     * @param eth_core ETH core to read the training status for.
     * @return Training status
     */
    EthTrainingStatus read_eth_core_training_status(CoreCoord eth_core);

    const SocDescriptor &get_soc_descriptor() const;

protected:
    LockManager lock_manager;

    // Every TTDevice is built around a model, which supplies its identity and the components it
    // runs on: the protocol it talks to hardware over, its architecture implementation and its SoC
    // architecture descriptor.
    explicit TTDevice(std::unique_ptr<TTDeviceModel> model);

    // Emulates a NOC multicast write by issuing a unicast write_to_device to every core in the
    // [core_start, core_end] grid. Simulation backends have no hardware multicast, so they delegate
    // their noc_multicast_write override here instead of duplicating the fallback loop.
    void multicast_write_via_unicast(
        const void *src,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        uint64_t addr,
        NocId noc_id = NocId::DEFAULT_NOC);

    void construct_soc_descriptor(const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    void set_soc_descriptor(const SocDescriptor &soc_descriptor);

private:
    // Wires the model's hang detector to this device: routes a timed-out MMIO op to a NOC liveness
    // check, and gives the detector a separately-locked window to probe through.
    void wire_hang_detector();

    xy_pair resolve_coordinate(CoreCoord core, NocId noc_id) const;

    DmaInterface *get_dma_interface();

    std::unique_ptr<TTDeviceModel> model_;
    std::optional<SocDescriptor> soc_descriptor_ = std::nullopt;
};

}  // namespace tt::umd
