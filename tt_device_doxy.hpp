// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

class FirmwareMessenger;
class FirmwareTelemetryReader;
class IoWindow;
class architecture_implementation;
class DeviceProtocol;
class RemoteCommunication;
class JtagDevice;
class JtagInterface;
class PCIDevice;
class PcieInterface;
class RemoteInterface;
class FirmwareBundleVersion;
class FirmwareInfoProvider;
class SocDescriptor;
class LockManager;
class HangDetector;

// enums
enum class NocId : uint8_t { DEFAULT = 0, NOC = 0, NOC0 = 0, NOC1 = 1, SYSTEM_NOC = 1 };
enum class RiscType : std::uint64_t;
enum class TensixSoftResetOptions : std::uint32_t;
enum class HostMapping { WC, UC };

enum class EthTrainingStatus {
    IN_PROGRESS = 0,
    SUCCESS = 1,
    FAIL = 2,
    NOT_CONNECTED = 3,
};

enum class IODeviceType {
    PCIe,
    JTAG,
    UNDEFINED,
};

enum class ARCH {
    WORMHOLE_B0 = 2,
    BLACKHOLE = 3,
    QUASAR = 4,
    Invalid = 0xFF,
};

enum BoardType : uint32_t {
    E75,
    E150,
    E300,
    N150,
    N300,
    P100,
    P150,
    P300,
    GALAXY,
    UBB,
    UBB_WORMHOLE = UBB,
    UBB_BLACKHOLE,
    QUASAR_BOARD,
    UNKNOWN,
};

struct HarvestingMasks {
    size_t tensix_harvesting_mask = 0;
    size_t dram_harvesting_mask = 0;
    size_t eth_harvesting_mask = 0;
    size_t pcie_harvesting_mask = 0;
    size_t l2cpu_harvesting_mask = 0;

    HarvestingMasks operator|(const HarvestingMasks &other) const {
        return HarvestingMasks{
            .tensix_harvesting_mask = this->tensix_harvesting_mask | other.tensix_harvesting_mask,
            .dram_harvesting_mask = this->dram_harvesting_mask | other.dram_harvesting_mask,
            .eth_harvesting_mask = this->eth_harvesting_mask | other.eth_harvesting_mask,
            .pcie_harvesting_mask = this->pcie_harvesting_mask | other.pcie_harvesting_mask,
            .l2cpu_harvesting_mask = this->l2cpu_harvesting_mask | other.l2cpu_harvesting_mask};
    }
};

struct CoreCoord;
struct io_window_config;

struct ChipInfo {
    bool noc_translation_enabled = false;
    HarvestingMasks harvesting_masks = {0, 0, 0, 0};
    BoardType board_type = BoardType::UNKNOWN;
    uint64_t board_id = 0;
    uint8_t asic_location = 0;
};

namespace timeout {
inline constexpr auto NON_MMIO_RW_TIMEOUT = std::chrono::milliseconds(5'000);
inline constexpr auto ARC_MESSAGE_TIMEOUT = std::chrono::milliseconds(1'000);
inline constexpr auto ARC_STARTUP_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto STARTUP_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto ARC_POST_RESET_TIMEOUT = std::chrono::milliseconds(1'000);
inline constexpr auto ARC_LONG_POST_RESET_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto DRAM_TRAINING_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto ETH_QUEUE_ENABLE_TIMEOUT = std::chrono::milliseconds(30'000);
inline constexpr auto ETH_TRAINING_TIMEOUT = std::chrono::milliseconds(900'000);
inline constexpr auto ETH_STARTUP_TIMEOUT = std::chrono::milliseconds(10'000);
inline constexpr auto ETH_HEARTBEAT_TIMEOUT = std::chrono::milliseconds(50);
inline constexpr auto AICLK_TIMEOUT = std::chrono::milliseconds(100);
inline constexpr auto WARM_RESET_M3_TIMEOUT = std::chrono::milliseconds(20'000);
inline constexpr auto WARM_RESET_REAPPEAR_POLL_INTERVAL = std::chrono::milliseconds(100);
inline constexpr auto WARM_RESET_DEVICES_REAPPEAR_TIMEOUT = std::chrono::milliseconds(10'000);
inline constexpr auto UBB_WARM_RESET_TIMEOUT = std::chrono::milliseconds(100'000);
inline constexpr auto BH_WARM_RESET_TIMEOUT = std::chrono::milliseconds(2'000);
}  // namespace timeout

static constexpr uint32_t HANG_READ_VALUE = 0xFFFFFFFFu;

class TTDevice {
public:
    /**
     * @brief Creates a TTDevice instance for a locally attached physical device.
     *
     * This factory method is used for hardware that the host system can directly
     * enumerate and access via memory-mapped IO (MMIO), such as devices on the PCIe
     * bus or a JTAG chain. The underlying physical transport interface (PCIDevice or
     * JtagDevice) is instantiated internally based on the provided topology index.
     *
     * @param device_number The zero-based index of the device as enumerated by the host OS.
     * @param device_type   The transport protocol to establish (PCIe or JTAG). Defaults to PCIe.
     * @param use_safe_api  If true, enables additional runtime boundary checks, safe handlers,
     * and validations for device interactions.
     * @return std::unique_ptr<TTDevice> A fully instantiated device orchestrator ready for initialization.
     */
    static std::unique_ptr<TTDevice> create(
        int device_number, IODeviceType device_type = IODeviceType::PCIe, bool use_safe_api = false);

    /**
     * @brief Creates a TTDevice instance for a standalone, network-attached accelerator.
     *
     * This factory method is used for devices reachable over Ethernet rather than a
     * local physical bus. Because the host OS cannot natively enumerate standalone network
     * hardware using a simple integer index, the client must establish the network
     * routing, IP, and socket state externally and inject it into the factory.
     *
     * @param remote_communication A uniquely owned, pre-configured network handler
     * established to communicate with the target Ethernet device.
     * @return std::unique_ptr<TTDevice> A fully instantiated device orchestrator ready for initialization.
     */
    static std::unique_ptr<TTDevice> create(std::unique_ptr<RemoteCommunication> remote_communication);

    /**
     * @brief Virtual destructor for TTDevice.
     * * Ensures proper cleanup of derived classes, which is specifically
     * critical when using derived classes (e.g. in testing, etc.).
     */
    virtual ~TTDevice() = default;

    /**
     * @brief Retrieves the architecture-specific implementation handler.
     * * Provides access to the underlying hardware architecture definitions
     * (e.g., Wormhole, Blackhole) managed by this device.
     * * @return architecture_implementation* Pointer to the architecture implementation.
     */
    architecture_implementation *get_architecture_implementation();

    /**
     * @brief Retrieves the underlying physical PCIe device interface.
     * * Acts as an "escape hatch" for executing transport-specific PCIe operations
     * that do not belong in the generic DeviceProtocol abstraction.
     * * @return PCIDevice* Pointer to the local PCIe device, or nullptr if this
     * TTDevice is not connected via PCIe.
     */
    PCIDevice *get_pci_device();

    /**
     * @brief Retrieves the underlying physical JTAG device interface.
     * * Acts as an "escape hatch" for executing transport-specific JTAG operations
     * (e.g., direct TAP controller manipulation) that do not belong in the
     * generic DeviceProtocol abstraction.
     * * @return JtagDevice* Pointer to the local JTAG device, or nullptr if this
     * TTDevice is not connected via JTAG.
     */
    JtagDevice *get_jtag_device();

    /**
     * @brief Retrieves the underlying Remote Ethernet communication interface.
     * * Acts as an "escape hatch" for executing network-specific operations
     * (e.g., querying MAC/IP addresses, checking socket health) for standalone
     * Ethernet-attached accelerators.
     * * @return RemoteCommunication* Pointer to the remote communication handler,
     * or nullptr if this TTDevice is not connected over an Ethernet network.
     */
    RemoteCommunication *get_remote_communication();

    /**
     * @brief Retrieves the base device protocol interface.
     * * Provides the common I/O operations supported across all transports:
     * - Unordered block data read/writes.
     * - Ordered register read/writes.
     * - Core range multicast writes.
     * * @return DeviceProtocol* Pointer to the base protocol interface.
     */
    DeviceProtocol *get_device_protocol();

    /**
     * @brief Retrieves the PCIe-specific capability interface.
     * * Used to access PCIe-only transport features, such as hardware DMA.
     * * @return PcieInterface* Pointer to the PCIe interface, or nullptr if the active
     * transport is not PCIe.
     */
    PcieInterface *get_pcie_interface();

    /**
     * @brief Retrieves the JTAG-specific capability interface.
     * * Used to access JTAG-only transport features and state machine controls.
     * * @return JtagInterface* Pointer to the JTAG interface, or nullptr if the active
     * transport is not JTAG.
     */
    JtagInterface *get_jtag_interface();

    /**
     * @brief Retrieves the Remote (Ethernet) capability interface.
     * * Used to access network-specific features for standalone Ethernet-attached devices.
     * * @return RemoteInterface* Pointer to the Remote interface, or nullptr if the active
     * transport is not Remote/Ethernet.
     */
    RemoteInterface *get_remote_interface();

    /**
     * @brief Retrieves the hardware architecture of the device.
     * @return ARCH Enum representing the architecture (e.g., WORMHOLE_B0, BLACKHOLE).
     */
    ARCH get_arch() const;

    /**
     * @brief Controls what happens when a hang is confirmed.
     */
    enum class HangAction {
        THROW,   ///< Throw std::runtime_error (default).
        RETURN,  ///< Return instead of throwing.
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

    /**
     * @brief Reads an unordered block of data from the device.
     * * Optimized for performance with no memory ordering guarantees.
     * * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_tt_device() to be invoked first to generate the SocDescriptor.
     * @param dst Destination host memory pointer.
     * @param core Target core coordinates.
     * @param addr Source address on the device.
     * @param size Number of bytes to read.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    virtual void read_data(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Writes an unordered block of data to the device.
     * * Optimized for performance with no memory ordering guarantees.
     * * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_tt_device() to be invoked first.
     * @param src Source host memory pointer.
     * @param core Target core coordinates.
     * @param addr Destination address on the device.
     * @param size Number of bytes to write.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    virtual void write_data(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Reads from device registers with strict memory ordering guarantees.
     * * Bypasses performance optimizations to ensure safe access to control and status registers (CSRs).
     * * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_tt_device() to be invoked first.
     * @param dst Destination host memory pointer.
     * @param core Target core coordinates.
     * @param addr Source register address on the device.
     * @param size Number of bytes to read.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    virtual void read_regs(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Writes to device registers with strict memory ordering guarantees.
     * * Guarantees that the write is flushed and ordered correctly for safe CSR manipulation.
     * * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_tt_device() to be invoked first.
     * @param src Source host memory pointer.
     * @param core Target core coordinates.
     * @param addr Destination register address on the device.
     * @param size Number of bytes to write.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    virtual void write_regs(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    /**
     * @brief Broadcasts data to a specific rectangular grid of cores via NOC multicast.
     * * Coordinate constraint: CoordSystem::LITERAL bypasses translation and is always valid.
     * All other coordinate systems require init_tt_device() to be invoked first.
     * @param src Source host memory pointer.
     * @param size Number of bytes to write.
     * @param core_start Starting core coordinates of the multicast grid.
     * @param core_end Ending core coordinates of the multicast grid.
     * @param addr Destination address on the cores.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    virtual void write_to_core_range(
        const void *src,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        uint64_t addr,
        NocId noc = NocId::DEFAULT);

    /**
     * @brief Broadcasts data to all TENSIX cores on the device via NOC multicast.
     * * Implicitly targets the entire compute grid.
     * @param src Source host memory pointer.
     * @param size Number of bytes to write.
     * @param addr Destination address on the cores.
     * @param noc Physical network to route the transaction over. Defaults to NocId::DEFAULT.
     */
    virtual void write_to_core_range(const void *src, size_t size, uint64_t addr, NocId noc = NocId::DEFAULT) = 0;

    /**
     * @brief Executes a Host-to-Device (H2D) DMA transfer using an internal bounce buffer.
     * * The driver handles copying the standard host memory into an internal pinned
     * staging buffer before executing the hardware DMA.
     * @param src Pointer to the standard host memory containing the data to send.
     * @param dst_addr The destination address on the target device core.
     * @param size The number of bytes to transfer.
     * @param core The target core coordinate on the device.
     */
    virtual void dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core);

    /**
     * @brief Executes a Device-to-Host (D2H) DMA transfer using an internal bounce buffer.
     * * The driver DMAs the data to an internal pinned staging buffer and then
     * copies it into the provided standard host memory pointer.
     * @param dst Pointer to the standard host memory where data will be received.
     * @param src_addr The source address on the target device core.
     * @param size The number of bytes to transfer.
     * @param core The source core coordinate on the device.
     */
    virtual void dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core);

    /**
     * @brief Executes a multicast Host-to-Device DMA transfer using an internal bounce buffer.
     * * Broadcasts data to a rectangular grid of cores.
     * @param src Pointer to the standard host memory containing the data to send.
     * @param dst_addr The destination address on the target device cores.
     * @param size The number of bytes to transfer.
     * @param core_start The top-left core coordinate of the multicast grid.
     * @param core_end The bottom-right core coordinate of the multicast grid.
     */
    virtual void dma_multicast_write(
        const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end);

    /**
     * @brief Executes a true zero-copy Host-to-Device (H2D) DMA transfer.
     * * Operates directly on caller-managed pinned host memory using its I/O Virtual Address.
     * @param src_iova The physical or I/O Virtual Address of the source host memory buffer.
     * @param dst_addr The destination address on the target device core.
     * @param size The number of bytes to transfer.
     * @param core The target core coordinate on the device.
     */
    virtual void dma_write(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core);

    /**
     * @brief Executes a true zero-copy Device-to-Host (D2H) DMA transfer.
     * * Operates directly on caller-managed pinned host memory using its I/O Virtual Address.
     * @param dst_iova The physical or I/O Virtual Address of the destination host memory buffer.
     * @param src_addr The source address on the target device core.
     * @param size The number of bytes to transfer.
     * @param core The source core coordinate on the device.
     */
    virtual void dma_read(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core);

    /**
     * @brief Executes a true zero-copy multicast Host-to-Device DMA transfer.
     * * Broadcasts data to a rectangular grid of cores directly from pinned host memory.
     * @param src_iova The physical or I/O Virtual Address of the source host memory buffer.
     * @param dst_addr The destination address on the target device cores.
     * @param size The number of bytes to transfer.
     * @param core_start The top-left core coordinate of the multicast grid.
     * @param core_end The bottom-right core coordinate of the multicast grid.
     */
    virtual void dma_multicast_write(
        uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end);

    /**
     * @brief Binds a host memory buffer IO address to the device's address.
     * * Abstracts hardware-specific translation mechanisms (e.g., PCIe iATU, lookup tables)
     * to allow device-side hardware and software to directly access host system memory.
     * @param host_io_address The physical address (PA) or I/O Virtual Address (IOVA) of the host buffer.
     * @param device_address The device-side address (e.g., NOC mapped address) targeting the host memory.
     * @param size The size of the memory window to bind in bytes.
     */
    virtual void bind_host_memory(uint64_t host_io_address, uint64_t device_address, size_t size);

    /**
     * @brief Retrieves the hardware identity and physical configuration of the chip.
     * * Provides the actual physical state of the device, including:
     * - The active state of NOC address translation.
     * - Bitmasks indicating which functional blocks (e.g., compute cores, DRAM) are harvested (disabled).
     * - Physical board topology, including board type, unique board ID, and specific ASIC location.
     * @return ChipInfo Struct containing the chip's physical state and identity.
     */
    virtual ChipInfo get_chip_info();

    /**
     * @brief Retrieves the version of the firmware bundle currently running on the device.
     * * Returns the combined semantic version of the loaded firmware bundle rather than
     * individual component versions. Useful for host-side compatibility verification
     * and diagnostics.
     * @return FirmwareBundleVersion The semantic version of the active firmware.
     */
    FirmwareBundleVersion get_firmware_version();

    /**
     * @brief Waits for the device firmware to signal that it is fully initialized and operational.
     * * Guarantees the device is in a correct state for subsequent operations.
     * * Must be successfully called before initiating communication via the FirmwareMessenger.
     * @param timeout_ms Maximum duration to wait for the firmware startup sequence.
     */
    virtual void wait_firmware_startup(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) = 0;

    /**
     * @brief Waits for the specified Ethernet core to complete its link training sequence.
     * @param eth_core Target Ethernet core coordinates.
     * @param timeout_ms Maximum duration to wait for training completion.
     * @return std::chrono::milliseconds Elapsed time taken for the training to complete.
     */
    virtual std::chrono::milliseconds wait_eth_core_training(
        const CoreCoord eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) = 0;

    /**
     * @brief Waits for the specified DRAM channel to complete its hardware training and calibration.
     * @param dram_channel The index of the DRAM channel to poll.
     * @param timeout_ms Maximum duration to wait for training completion.
     */
    void wait_dram_channel_training(
        const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms = timeout::DRAM_TRAINING_TIMEOUT);

    /**
     * @brief Retrieves the interface for sending commands to the device's management firmware.
     * * Abstracts away the specific firmware implementation.
     * @return FirmwareMessenger* Pointer to the firmware messaging interface.
     */
    FirmwareMessenger *get_firmware_messenger() const;

    /**
     * @brief Retrieves the interface for reading telemetry published by the device's firmware.
     * * Used to monitor device health, temperatures, voltages, and runtime status.
     * @return FirmwareTelemetryReader* Pointer to the firmware telemetry interface.
     */
    FirmwareTelemetryReader *get_firmware_telemetry_reader() const;

    /**
     * @brief Retrieves the provider interface for querying static firmware metadata.
     * * Used to fetch bundle configurations and versioning details.
     * @return FirmwareInfoProvider* Pointer to the firmware info provider.
     */
    FirmwareInfoProvider *get_firmware_info_provider() const;

    /**
     * @brief Defines the requested power domain state for the device.
     */
    enum class PowerState {
        BUSY,  ///< Claims all power domains, requesting maximum performance.
        IDLE   ///< Releases power domains, allowing the device to enter lower power states.
    };

    /**
     * @brief Requests a specific power state from the Kernel Mode Driver (KMD).
     * * Acts as a hint to the KMD to either claim or release full power domains.
     * * Note: This is a no-op for remote devices and for local devices running KMD
     * versions older than 2.6.0.
     * @param state The requested power state (BUSY or IDLE).
     */
    virtual void set_power_state(PowerState state);

    /**
     * @brief Retrieves the current operating clock frequency of the device.
     * @return uint32_t Current clock frequency (typically in MHz).
     */
    virtual uint32_t get_clock_freq() const;

    /**
     * @brief Retrieves the maximum supported clock frequency of the device.
     * @return uint32_t Maximum clock frequency (typically in MHz).
     */
    virtual uint32_t get_max_clock_freq() const;

    /**
     * @brief Retrieves the minimum supported clock frequency of the device.
     * @return uint32_t Minimum clock frequency (typically in MHz).
     */
    virtual uint32_t get_min_clock_freq() const;

    /**
     * @brief Retrieves the unique physical identifier of the board hosting the chip.
     * @return uint64_t Unique board identifier.
     */
    virtual uint64_t get_board_id() const;

    /**
     * @brief Retrieves the physical slot index of this chip on a multi-chip board.
     * @return uint8_t ASIC location index.
     */
    virtual uint8_t get_asic_location() const;

    /**
     * @brief Retrieves the hardware model or SKU of the board.
     * @return BoardType Enum representing the board type (e.g., e75, N300).
     */
    virtual BoardType get_board_type() const;

    /**
     * @brief Checks if NOC coordinate translation is currently active on the device.
     * * When active, core coordinates have a different translation table which
     * accounts for hardware harvesting (disabled rows/columns).
     * @return true if translation is enabled, false otherwise.
     */
    virtual bool get_noc_translation_enabled() const = 0;

    /**
     * @brief Retrieves the current operating temperature of the ASIC.
     * @return double Temperature in degrees Celsius.
     */
    double get_asic_temperature() const;

    /**
     * @brief Blocks until all in-flight, non-MMIO data writes have reached their destination.
     * * Used to guarantee memory consistency. Ensures that posted bulk data transfers
     * (which lack strict memory ordering guarantees) are fully flushed and visible in
     * device memory before proceeding.
     */
    virtual void wait_for_non_mmio_flush();

    /**
     * @brief Indicates whether the device is accessed via a remote network transport.
     * @return true if the device is remote.
     */
    virtual bool is_remote() const;

    /**
     * @brief Executes the full device initialization sequence.
     * * This includes waiting for firmware startup, polling necessary hardware states,
     * and generating the SocDescriptor required for NOC coordinate translation.
     * @param timeout_ms Maximum time to wait for the firmware/hardware to become ready.
     * @param soc_descriptor_path Optional path to a specific SoC descriptor file. If empty,
     * the descriptor is dynamically generated or read from the default hardware configuration.
     */
    void init_device(
        std::chrono::milliseconds timeout_ms = timeout::STARTUP_TIMEOUT, const std::string &soc_descriptor_path = "");

    /**
     * @brief Retrieves the current value of the hardware's free-running reference clock counter.
     * * Useful for on-device performance profiling, latency measurements, and timestamping.
     * @return uint64_t The current reference clock tick count.
     */
    uint64_t get_refclk_counter() const;

    /**
     * @brief Retrieves the identifier of the underlying communication link.
     * * Depending on the transport, this maps to the local PCIe device index, J-Link ID, or remote node ID.
     * @return int The communication device identifier.
     */
    int get_communication_device_id() const;

    /**
     * @brief Retrieves the transport mechanism currently used to communicate with this device.
     * @return IODeviceType Enum representing the transport type (e.g., PCIe, JTAG, Remote).
     */
    IODeviceType get_communication_device_type() const;

    /**
     * @brief Returns which RISC processors are currently held in soft reset.
     * @param core Target core coordinates.
     * @return Bitmask of RISCs currently in reset.
     */
    virtual RiscType get_risc_reset_state(CoreCoord core);

    /**
     * @brief Asserts the soft reset signal for specific RISC processors on a given core.
     * Halts the execution of the targeted RISCs, putting them in a safe state for binary loading.
     * @param core Target core coordinates.
     * @param selected_riscs Strongly typed bitmask specifying which RISCs to reset.
     */
    virtual void assert_risc_reset(CoreCoord core, const RiscType selected_riscs);

    /**
     * @brief Deasserts the soft reset signal, allowing the specified RISC processors to begin execution.
     * @param core Target core coordinates.
     * @param selected_riscs Strongly typed bitmask specifying which RISCs to release from reset.
     * @param staggered_start If true, staggers the startup of the RISCs to mitigate sudden power draw spikes. Defaults
     * to false.
     */
    virtual void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start = false);

    /**
     * @brief Broadcasts a soft reset to all Tensix cores across the entire compute grid.
     * The base TTDevice implementation throws by default. Subclasses (e.g., PCIe/Wormhole)
     * override this to implement hardware-accelerated all-core reset semantics.
     * @param soft_resets Strongly typed configuration struct defining the reset behavior.
     */
    virtual void send_tensix_risc_reset(const TensixSoftResetOptions &soft_resets);

    /**
     * @brief Creates and allocates an I/O window for host-to-device memory-mapped access.
     * * The default implementation uses the underlying hardware driver (e.g., PCIDevice)
     * to allocate a hardware TLB, while simulation subclasses allocate from their specific backends.
     * @param config The hardware configuration applied to the newly created I/O window.
     * @param mapping The host memory mapping strategy to use (e.g., HostMapping::WC for Write-Combining,
     * HostMapping::UC for Uncacheable). Defaults to WC for higher write throughput.
     * @param size Requested window size in bytes. A value of 0 instructs the driver to attempt
     * architecture-supported sizes in descending order.
     * @return std::unique_ptr<IoWindow> An exclusively owned handle to the newly created I/O window.
     */
    virtual std::unique_ptr<IoWindow> create_io_window(
        io_window_config config, HostMapping mapping = HostMapping::WC, size_t size = 0);

    /**
     * @brief Configures a safe signal handler for SIGBUS errors.
     * * Useful for preventing hard crashes when a PCIe device drops off the bus or when
     * accessing unmapped/invalid device memory regions during runtime.
     * @param set_safe_handler If true, installs the safe handler. If false, restores default behavior.
     */
    static void set_sigbus_safe_handler(bool set_safe_handler);

    /**
     * @brief Reads the hardware link training status of a specific Ethernet core.
     * * Used to verify if the ethernet links between remote devices have successfully initialized.
     * @param eth_core The target Ethernet core coordinates.
     * @return EthTrainingStatus The current training status of the specified core.
     */
    virtual EthTrainingStatus read_eth_core_training_status(CoreCoord eth_core) = 0;

    /**
     * @brief Retrieves the System-on-Chip (SoC) descriptor for the device.
     * * The descriptor contains the physical topology of the chip, including grid sizes,
     * active/harvested core locations, and memory bank mapping details.
     * @return const SocDescriptor& Reference to the device's topology descriptor.
     */
    const SocDescriptor &get_soc_descriptor() const;

protected:
    /**
     * @brief Initializes a TTDevice over a local PCIe transport.
     * @param pci_device Pointer to the physical PCIe device.
     * @param architecture_impl Pointer defining the hardware architecture.
     * @param use_safe_api Enables runtime boundary checks and safe handlers.
     */
    TTDevice(
        std::unique_ptr<PCIDevice> pci_device,
        std::unique_ptr<architecture_implementation> architecture_impl,
        bool use_safe_api);

    /**
     * @brief Initializes a TTDevice over a local JTAG transport.
     * @param jtag_device Pointer to the physical JTAG device.
     * @param jlink_id Identifier for the connected J-Link probe.
     * @param architecture_impl Pointer defining the hardware architecture.
     */
    TTDevice(
        std::unique_ptr<JtagDevice> jtag_device,
        uint8_t jlink_id,
        std::unique_ptr<architecture_implementation> architecture_impl);

    /**
     * @brief Initializes a TTDevice over a Remote Ethernet transport.
     * @param remote_communication Pointer to the established network communication handler.
     * @param architecture_impl Pointer defining the hardware architecture.
     */
    TTDevice(
        std::unique_ptr<RemoteCommunication> remote_communication,
        std::unique_ptr<architecture_implementation> architecture_impl);

    TTDevice();
    TTDevice(std::unique_ptr<architecture_implementation> architecture_impl);

    /**
     * @brief Internal hook for derived classes to initiate a DRAM channel retraining sequence.
     * @param dram_channel The index of the DRAM channel to retrain.
     */
    virtual void retrain_dram_core(const uint32_t dram_channel) = 0;

    /**
     * @brief Defines how many times the device should attempt to retrain a failing DRAM link.
     * @return uint32_t Maximum number of retries (0 by default).
     */
    virtual uint32_t get_max_dram_retrain_attempts() const { return 0; }

    /**
     * @brief Injects a custom hang detector for monitoring device execution state.
     * @param hang_detector Exclusively owned pointer to the detector instance.
     */
    void set_hang_detector(std::unique_ptr<HangDetector> hang_detector);

    /**
     * @brief Builds the default SoC topology descriptor based on the detected hardware.
     * @param soc_descriptor_path Optional path to override the topology via file.
     */
    void construct_soc_descriptor(const std::string &soc_descriptor_path = "");

    /**
     * @brief Manually overrides the device's SoC topology descriptor.
     * @param soc_descriptor The descriptor struct containing core and memory layouts.
     */
    void set_soc_descriptor(const SocDescriptor &soc_descriptor);

private:
    /**
     * @brief Internal routine to discover and establish communication with the management firmware.
     */
    void probe_firmware();

    // --- Core Device State ---
    IODeviceType communication_device_type_ = IODeviceType::UNDEFINED;
    int communication_device_id_ = -1;
    ARCH arch_ = ARCH::Invalid;

    /**
     * @brief Pointer to the system lock manager.
     * * Note: Forward declared to decouple compilation dependencies.
     */
    LockManager *lock_manager_;  // actually not a pointer but depends will explain

    // --- Owned Subsystems ---
    std::unique_ptr<architecture_implementation> architecture_impl_;
    std::unique_ptr<DeviceProtocol> device_protocol_;
    std::unique_ptr<HangDetector> hang_detector_;
    std::unique_ptr<FirmwareMessenger> firmware_messenger_;
    std::unique_ptr<FirmwareTelemetryReader> firmware_telemetry_reader_;
    std::unique_ptr<FirmwareInfoProvider> firmware_info_provider_;

    // --- Capability Views (Non-owning pointers downcasted from device_protocol_) ---
    PcieInterface *pcie_interface_ = nullptr;
    JtagInterface *jtag_interface_ = nullptr;
    RemoteInterface *remote_interface_ = nullptr;
};

int main() { return 0; }
