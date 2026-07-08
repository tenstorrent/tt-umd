// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tt_enums_structs_constants_doxy.hpp"
#include "tt_soc_descriptor_doxy.hpp"

namespace tt::umd {

class TTDeviceModel;
class DeviceProtocol;
class DeviceFirmware;
class HangDetector;
class FirmwareTelemetryReader;
class FirmwareInfoProvider;
class ArchitectureImplementation;
class PcieInterface;
class DmaInterface;
class IoWindowFactory;
class IoWindow;
class JtagInterface;
class RemoteInterface;
class RemoteCommunication;

/**
 * @defgroup tt_device TTDevice
 * @{
 *
 * @brief TTDevice is the base layer facade and lifetime manager for a single Tenstorrent device.
 *
 * It owns the physical transport (PCIe, JTAG, or remote Ethernet), drives the
 * device initialization and teardown sequences, and exposes the primitives that
 * the Chip/Cluster workload layer builds on.
 *
 * ## Common Types
 *
 * The following types appear throughout the TTDevice API and are defined in
 * @ref tt_base_types "Base Layer Types and Constants".
 *
 * | Type | Description |
 * |------|-------------|
 * | CoreCoord | Physical core coordinate (row, col) with coordinate system tag and core type |
 * | xy_pair | Raw NOC coordinate pair |
 * | NocId | NOC selection |
 * | IODeviceType | Transport type (PCIe / JTAG) |
 * | ChipInfo | Device metadata (harvesting masks, board type, board ID) |
 * | RiscType | Bitmask identifying which RISCs to target |
 * | PowerState | Requested power domain state (BUSY / IDLE) |
 *
 */

class TTDevice final {
public:
    /**
     * @brief Controls what happens when a hang is confirmed.
     */
    enum class HangAction {
        THROW,   ///< Throw std::runtime_error (default).
        RETURN,  ///< Return a value instead of throwing.
    };

    /**
     * @brief Defines the requested power domain state for the device.
     */
    enum class PowerState {
        BUSY,  ///< Claims all power domains, requesting maximum performance.
        IDLE   ///< Releases power domains, allowing the device to enter lower power states.
    };

    TTDevice(std::unique_ptr<TTDeviceModel> model);

    ~TTDevice();

    /**
     * @brief Executes the full device initialization sequence.
     *
     * Waits for firmware startup, polls necessary hardware states, and generates the
     * SocDescriptor required for NOC coordinate translation.
     *
     * @param timeout_ms Maximum time to wait for the firmware/hardware to become ready.
     * Defaults to @ref timeout::FIRMWARE_STARTUP_TIMEOUT.
     * @param soc_descriptor_path Optional path to a specific SoC descriptor file. If empty,
     * the descriptor is dynamically generated or read from the default hardware configuration.
     */
    void init_device(
        std::chrono::milliseconds timeout_ms = timeout::FIRMWARE_STARTUP_TIMEOUT, NocId noc = NocId::DEFAULT);

    /**
     * @brief Reads a block of data from a device core into a host buffer, suited for bulk data transfers.
     *
     * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_device() to be invoked first.
     *
     * @param dst Destination host memory pointer.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to read.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void read_data(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Writes a block of host data to a device core, suited for bulk data transfers.
     *
     * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_device() to be invoked first.
     *
     * @param src Source host memory pointer.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to write.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void write_data(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Reads data from a device core into a host buffer, suited for register and control transactions.
     *
     * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_device() to be invoked first.
     *
     * @param dst Destination host memory pointer.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to read.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void read_ctrl(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Writes host data to a device core, suited for register and control transactions.
     *
     * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_device() to be invoked first.
     *
     * @param src Source host memory pointer.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to write.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void write_ctrl(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Broadcasts data to a specific rectangular grid of cores via NOC multicast.
     *
     * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_device() to be invoked first.
     *
     * @param src Source host memory pointer.
     * @param size Number of bytes to write.
     * @param core_start Starting core coordinates of the multicast grid.
     * @param core_end Ending core coordinates of the multicast grid.
     * @param addr Destination address on the cores.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void write_to_core_range(
        const void *src,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        uint64_t addr,
        NocId noc = NocId::DEFAULT);

    /**
     * @brief Broadcasts data to all TENSIX cores on the device via NOC multicast.
     *
     * Implicitly targets the entire compute grid.
     *
     * @param src Source host memory pointer.
     * @param size Number of bytes to write.
     * @param addr Destination address on the cores.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void write_to_core_range(const void *src, size_t size, uint64_t addr, NocId noc = NocId::DEFAULT);

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
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

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
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

    /**
     * @brief Executes a multicast Host-to-Device DMA transfer using an internal bounce buffer.
     *
     * Broadcasts data to a rectangular grid of cores via the internal staging buffer.
     *
     * @param src Pointer to the user-provided buffer containing the data to send.
     * @param dst_addr Destination address on the target device cores.
     * @param size Number of bytes to transfer.
     * @param core_start Top-left core coordinate of the multicast grid.
     * @param core_end Bottom-right core coordinate of the multicast grid.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void dma_write_to_core_range(
        const void *src,
        uint64_t dst_addr,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        NocId noc = NocId::DEFAULT);

    /**
     * @brief Executes a zero-copy Device-to-Host (D2H) DMA transfer.
     *
     * Operates directly on caller-managed pinned host memory identified by its IOVA,
     * bypassing the internal staging buffer.
     *
     * @param dst_iova IOVA of the destination pinned host memory buffer.
     * @param src_addr Source address on the target device core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinate on the device.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void dma_read_zero_copy(
        uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

    /**
     * @brief Executes a zero-copy Host-to-Device (H2D) DMA transfer.
     *
     * Operates directly on caller-managed pinned host memory identified by its IOVA,
     * bypassing the internal staging buffer.
     *
     * @param src_iova IOVA of the source pinned host memory buffer.
     * @param dst_addr Destination address on the target device core.
     * @param size Number of bytes to transfer.
     * @param core Target core coordinate on the device.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void dma_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

    /**
     * @brief Executes a zero-copy multicast Host-to-Device DMA transfer.
     *
     * Broadcasts data to a rectangular grid of cores directly from caller-managed
     * pinned host memory, bypassing the internal staging buffer.
     *
     * @param src_iova IOVA of the source pinned host memory buffer.
     * @param dst_addr Destination address on the target device cores.
     * @param size Number of bytes to transfer.
     * @param core_start Top-left core coordinate of the multicast grid.
     * @param core_end Bottom-right core coordinate of the multicast grid.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    void dma_write_to_core_range_zero_copy(
        uint64_t src_iova,
        uint64_t dst_addr,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        NocId noc = NocId::DEFAULT);

    /**
     * @brief Waits for the device firmware to signal that it is fully initialized and operational.
     *
     * Must be successfully called before initiating communication via the FirmwareMessenger.
     *
     * @param timeout_ms Maximum duration to wait for the firmware startup sequence.
     */
    void init_firmware(
        const std::chrono::milliseconds timeout_ms = timeout::FIRMWARE_STARTUP_TIMEOUT, NocId noc = NocId::DEFAULT);

    /**
     * @brief Waits for the specified Ethernet core to complete its link training sequence.
     * @param eth_core Target Ethernet core coordinates.
     * @param timeout_ms Maximum duration to wait for training completion.
     * Defaults to @ref timeout::ETH_TRAINING_TIMEOUT.
     * @return std::chrono::milliseconds Elapsed time taken for the training to complete.
     */
    bool wait_eth_core_training(
        const CoreCoord eth_core,
        const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT,
        NocId noc = NocId::DEFAULT);

    /**
     * @brief Waits for the specified DRAM channel to complete its hardware training and calibration.
     * @param dram_channel The index of the DRAM channel to poll.
     * @param timeout_ms Maximum duration to wait for training completion.
     * Defaults to @ref timeout::DRAM_TRAINING_TIMEOUT.
     */
    void wait_dram_channel_training(
        const uint32_t dram_channel,
        const std::chrono::milliseconds timeout_ms = timeout::DRAM_TRAINING_TIMEOUT,
        NocId noc = NocId::DEFAULT);

    /**
     * @brief Blocks until all in-flight, non-MMIO data writes have reached their destination.
     *
     * Guarantees that all transactions sent via remote communication have completed
     * before proceeding.
     */
    void wait_for_non_mmio_flush(NocId noc = NocId::DEFAULT);

    /**
     * @brief Check if the bus communication (usually PCIe) is hung.
     *
     * Reads a known register over BAR and compares the result against the hang
     * signature. If the device is not locally accessible (remote) or the device
     * doesn't have a "hung" value (e.g. JTAG) the check is skipped and false is returned.
     *
     * @param data_read  Value to compare against the hang signature. Defaults to
     *                   @ref HANG_READ_VALUE so callers can simply invoke is_bus_hung()
     *                   after any BAR read that returned a suspicious value.
     * @param action     What to do when a hang is confirmed. Defaults to Throw.
     * @return true if the bus communication appears hung. Only meaningful when action is RETURN.
     * @throws std::runtime_error if a hang is confirmed and action is THROW.
     */
    bool is_bus_hung(uint32_t data_read = HANG_READ_VALUE, HangAction action = HangAction::THROW);

    /**
     * @brief Check if NOC traffic to the device is hung.
     *
     * Sends a read to a register with a known value over the specified NOC and
     * compares the result against the hang signature. Only meaningful for locally accessible devices;
     * on remote devices the check is skipped and false is returned.
     *
     * @param noc     NOC to check.
     * @param action  What to do when a hang is confirmed. Defaults to Throw.
     * @return true if the NOC appears hung. Only meaningful when action is RETURN.
     * @throws std::runtime_error if a hang is confirmed and action is THROW.
     */
    bool is_noc_hung(NocId noc, HangAction action = HangAction::THROW);

    /**
     * @brief Returns which baby RISCs are currently held in soft reset.
     * @param core Target core coordinates.
     * @return Bitmask of RISCs currently in reset.
     */
    RiscType get_risc_reset_state(CoreCoord core, NocId noc = NocId::DEFAULT);

    /**
     * @brief Asserts the soft reset signal for specific baby RISCs on a given core.
     *
     * Halts the execution of the targeted RISCs, putting them in a safe state for binary loading.
     *
     * @param core Target core coordinates.
     * @param selected_riscs Bitmask specifying which RISCs to reset.
     */
    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc = NocId::DEFAULT);

    /**
     * @brief Deasserts the soft reset signal, allowing the specified baby RISCs to begin execution.
     * @param core Target core coordinates.
     * @param selected_riscs Bitmask specifying which RISCs to release from reset.
     * @param staggered_start If true, staggers the startup of the RISCs to mitigate sudden power draw spikes. Defaults
     * to false.
     */
    void deassert_risc_reset(
        CoreCoord core, const RiscType selected_riscs, bool staggered_start = false, NocId noc = NocId::DEFAULT);

    /**
     * @brief Creates an I/O window mapping a region of host virtual address space to device address space.
     *
     * The returned window supports direct pointer-style reads and writes to device memory.
     * It can be reconfigured at runtime to point to different device addresses.
     *
     * @param target Device-side target describing the core, address, and optional NOC.
     * See @ref TargetIoWindowConfig.
     * @param host Host-side properties (caching strategy and requested size). A size of 0
     *        delegates size selection to the concrete implementation.
     * See @ref HostIoWindowConfig.
     * @return std::unique_ptr<@ref IoWindow> An exclusively owned handle to the newly created I/O window.
     */
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host);

    /**
     * @brief Sends a command to the device's management firmware and waits for the result.
     *
     * Delegates to the underlying FirmwareMessenger. Acquires the device lock
     * before forwarding the command.
     *
     * @param msg_code Command identifier understood by the firmware.
     * @param args Arguments for the command (device-specific limits apply).
     * @param timeout Timeout for the command to complete.
     * @return DeviceCommandResult The exit code and any return values from the firmware.
     */
    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t> &args = {},
        std::chrono::milliseconds timeout = timeout::FIRMWARE_STARTUP_TIMEOUT,
        NocId noc = NocId::DEFAULT);

    /**
     * @brief Retrieves the base device protocol interface.
     *
     * Provides the common I/O operations supported across all transports:
     * data read/writes, control read/writes, and core range multicast writes.
     *
     * @return @ref DeviceProtocol* Pointer to the base protocol interface.
     */
    DeviceProtocol *get_device_protocol();

    /**
     * @brief Retrieves the PCIe-specific capability interface.
     *
     * Provides access to hardware DMA transfers, NOC multicast writes,
     * and direct BAR register access.
     *
     * @return @ref PcieInterface* Pointer to the PCIe interface, or nullptr if the
     * active transport is not PCIe.
     */
    PcieInterface *get_pcie_interface();

    /**
     * @brief Retrieves the JTAG-specific capability interface.
     *
     * Provides access to the underlying JTAG device for AXI/NOC reads and writes,
     * TDR register access, debug bus operations, and J-Link management.
     *
     * @return @ref JtagInterface* Pointer to the JTAG interface, or nullptr if the
     * active transport is not JTAG.
     */
    JtagInterface *get_jtag_interface();

    /**
     * @brief Retrieves the Remote Ethernet capability interface.
     *
     * Provides access to the underlying remote communication handler
     * and non-MMIO flush synchronization.
     *
     * @return RemoteInterface* Pointer to the Remote interface, or nullptr if the
     * active transport is not Remote/Ethernet.
     */
    RemoteInterface *get_remote_interface();

    /**
     * @brief Retrieves the interface for reading telemetry published by the device's firmware.
     * @return FirmwareTelemetryReader* Pointer to the firmware telemetry interface.
     */
    FirmwareTelemetryReader *get_firmware_telemetry_reader() const;

    /**
     * @brief Retrieves the provider interface for querying firmware metadata and device telemetry.
     * @return FirmwareInfoProvider* Pointer to the firmware info provider.
     */
    FirmwareInfoProvider *get_firmware_info_provider() const;

    /**
     * @brief Retrieves the underlying Remote Ethernet communication handler.
     *
     * Lowest layer in the stack. Only use when DeviceProtocol and RemoteInterface
     * are not sufficient.
     *
     * @return RemoteCommunication* Pointer to the remote communication handler,
     * or nullptr if this TTDevice is not reachable via MMIO.
     */
    RemoteCommunication *get_remote_communication();

    /** @name Device Identity and Topology */
    /** @{ */

    /**
     * @brief Retrieves the hardware architecture of the device.
     * @return @ref ARCH Enum representing the architecture (e.g., WORMHOLE_B0, BLACKHOLE).
     */
    ARCH get_arch() const;

    /**
     * @brief Retrieves the hardware identity and physical configuration of the chip.
     *
     * Returns the actual physical state of the device, including:
     * - The active state of NOC address translation.
     * - Bitmasks indicating which functional blocks (e.g., Tensix cores, DRAM cores) are harvested (disabled).
     * - Physical board topology, including board type, unique board ID, and specific ASIC location.
     *
     * @return ChipInfo Struct containing the chip's physical state and identity.
     */
    ChipInfo get_chip_info(NocId noc = NocId::DEFAULT);

    /**
     * @brief Retrieves the System-on-Chip (SoC) descriptor for the device.
     *
     * Contains the physical topology of the chip, including grid sizes,
     * active/harvested core locations, and memory bank mapping details.
     * Constructed during init_device() from static architecture data and
     * runtime ChipInfo.
     *
     * @return const @ref SocDescriptor& Reference to the device's topology descriptor.
     */
    const SocDescriptor &get_soc_descriptor() const;

    /**
     * @brief Retrieves the version of the firmware bundle currently running on the device.
     *
     * Returns the combined semantic version of the loaded firmware bundle rather than
     * individual component versions.
     *
     * @return @ref FirmwareBundleVersion The semantic version of the active firmware.
     */
    FirmwareBundleVersion get_firmware_version(NocId noc = NocId::DEFAULT);

    /**
     * @brief Checks if NOC coordinate translation is currently active on the device.
     *
     * When active, core coordinates use a translation table that accounts for
     * hardware harvesting (disabled rows/columns).
     *
     * @return true if translation is enabled, false otherwise.
     */
    bool get_noc_translation_enabled() const;

    /**
     * @brief Retrieves the unique physical identifier of the board hosting the chip.
     * @return uint64_t Unique board identifier.
     */
    uint64_t get_board_id() const;

    /**
     * @brief Retrieves the physical slot index of this chip on a multi-chip board.
     * @return uint8_t ASIC location index.
     */
    uint8_t get_asic_location() const;

    /**
     * @brief Retrieves the hardware model or SKU of the board.
     * @return @ref BoardType Enum representing the board type (e.g., e75, N300).
     */
    BoardType get_board_type() const;

    /**
     * @brief Retrieves the architecture-specific implementation handler.
     * @return @ref ArchitectureImplementation* Pointer to the architecture implementation.
     */
    ArchitectureImplementation *get_architecture_implementation();

    /**
     * @brief Indicates whether the device is accessed via a remote network transport.
     * @return true if the device is remote.
     */
    bool is_remote() const;

    /** @} */

    /** @name Clock and Thermal */
    /** @{ */

    /**
     * @brief Retrieves the current operating temperature of the ASIC.
     * @return double Temperature in degrees Celsius.
     */
    double get_asic_temperature(NocId noc = NocId::DEFAULT) const;

    /**
     * @brief Retrieves the current operating clock frequency of the device.
     * @return uint32_t Current clock frequency (typically in MHz).
     */
    uint32_t get_clock_freq(NocId noc = NocId::DEFAULT) const;

    /**
     * @brief Retrieves the maximum supported clock frequency of the device.
     * @return uint32_t Maximum clock frequency (typically in MHz).
     */
    uint32_t get_max_clock_freq(NocId noc = NocId::DEFAULT) const;

    /**
     * @brief Retrieves the minimum supported clock frequency of the device.
     * @return uint32_t Minimum clock frequency (typically in MHz).
     */
    uint32_t get_min_clock_freq(NocId noc = NocId::DEFAULT) const;

    /**
     * @brief Retrieves the current value of the hardware's free-running reference clock counter.
     * @return uint64_t The current reference clock tick count.
     */
    uint64_t get_refclk_counter(NocId noc = NocId::DEFAULT) const;

    /** @} */

    /** @name Communication */
    /** @{ */

    /**
     * @brief Retrieves the identifier of the underlying communication link.
     *
     * Depending on the transport, this maps to the local PCIe device index,
     * J-Link ID, or remote node ID.
     *
     * @return int The communication device identifier.
     */
    int get_communication_device_id() const;

    /**
     * @brief Retrieves the transport mechanism currently used to communicate with this device.
     * @return IODeviceType Enum representing the transport type (e.g., PCIe, JTAG, Remote).
     */
    IODeviceType get_communication_device_type() const;

    /**
     * @brief Returns the NUMA node associated with this device.
     * @return int NUMA node ID, or -1 if the device is not PCIe-connected or the system is non-NUMA.
     */
    int get_numa_node() const;

    /**
     * @brief Reads the hardware link training status of a specific Ethernet core.
     * @param eth_core The target Ethernet core coordinates.
     * @return @ref EthTrainingStatus The current training status of the specified core.
     */
    EthTrainingStatus get_eth_core_training_status(CoreCoord eth_core, NocId noc = NocId::DEFAULT);

    /** @} */

    /**
     * @brief Requests a hardware power domain state change.
     *
     * Claims or releases full power domains. No-op for remote devices.
     *
     * @param state The requested power state (BUSY or IDLE).
     */
    void set_power_state(PowerState state, NocId noc = NocId::DEFAULT);

    /**
     * @brief Sets the device clock frequency.
     *
     * Controls the AICLK frequency the device runs at. Distinct from
     * set_power_state(), which manages hardware power domains.
     *
     * @param state The target clock state (BUSY = max frequency, IDLE = min frequency).
     */
    void set_clock_state(PowerState state, NocId noc = NocId::DEFAULT);

    /**
     * @brief Installs or removes a safe SIGBUS signal handler.
     *
     * When a PCIe device is reset or drops off the bus, existing MMIO mappings
     * become stale. Accessing them triggers SIGBUS, which by default kills the
     * process. Once this handler is installed, such faults raise a C++ exception
     * instead, giving the caller a chance to tear down the stale mappings and
     * re-establish new ones.
     *
     * @param set_safe_handler If true, installs the safe handler. If false, restores default behavior.
     */
    static void set_sigbus_safe_handler(bool set_safe_handler);

private:
    std::unique_ptr<TTDeviceModel> model_;

    DeviceProtocol *device_protocol_;
    DeviceFirmware *device_firmware_;
    HangDetector *hang_detector_;
    FirmwareTelemetryReader *firmware_telemetry_reader_;
    FirmwareInfoProvider *firmware_info_provider_;
    ArchitectureImplementation *architecture_impl_;

    PcieInterface *pcie_interface_ = nullptr;
    DmaInterface *dma_interface_ = nullptr;
    IoWindowFactory *io_window_factory_ = nullptr;
    JtagInterface *jtag_interface_ = nullptr;
    RemoteInterface *remote_interface_ = nullptr;

    tt_xy_pair translate(CoreCoord core) const;

    std::optional<SocDescriptor> soc_descriptor_;
    bool is_remote_ = false;
};

/** @} */  // end of tt_device group

}  // namespace tt::umd
