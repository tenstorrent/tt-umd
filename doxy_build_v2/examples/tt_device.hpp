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

class TTDevice final {
public:
    enum class HangAction {
        THROW,   ///< Throw std::runtime_error (default).
        RETURN,  ///< Return a value instead of throwing.
    };

    enum class PowerState {
        BUSY,  ///< Claims all power domains, requesting maximum performance.
        IDLE   ///< Releases power domains, allowing the device to enter lower power states.
    };

    TTDevice(std::unique_ptr<TTDeviceModel> model);

    ~TTDevice();

    void init_device(std::chrono::milliseconds timeout_ms = timeout::FIRMWARE_STARTUP_TIMEOUT);

    void read_data(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    void write_data(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    void read_ctrl(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    void write_ctrl(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT);

    void write_to_core_range(
        const void *src,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        uint64_t addr,
        NocId noc = NocId::DEFAULT);

    void write_to_core_range(const void *src, size_t size, uint64_t addr, NocId noc = NocId::DEFAULT);

    void dma_write_to_core_range(
        const void *src, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

    void dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

    void dma_write(
        const void *src,
        uint64_t dst_addr,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        NocId noc = NocId::DEFAULT);

    void dma_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

    void dma_read_zero_copy(
        uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core, NocId noc = NocId::DEFAULT);

    void dma_write_to_core_range_zero_copy(
        uint64_t src_iova,
        uint64_t dst_addr,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        NocId noc = NocId::DEFAULT);

    void init_firmware(const std::chrono::milliseconds timeout_ms = timeout::FIRMWARE_STARTUP_TIMEOUT);

    bool wait_eth_core_training(
        const CoreCoord eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);

    void wait_dram_channel_training(
        const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms = timeout::DRAM_TRAINING_TIMEOUT);

    void wait_for_non_mmio_flush();

    bool is_bus_hung(uint32_t data_read = HANG_READ_VALUE, HangAction action = HangAction::THROW);

    bool is_noc_hung(NocId noc, HangAction action = HangAction::THROW);

    RiscType get_risc_reset_state(CoreCoord core, NocId noc = NocId::DEFAULT);

    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs, NocId noc = NocId::DEFAULT);

    void deassert_risc_reset(
        CoreCoord core, const RiscType selected_riscs, bool staggered_start = false, NocId noc = NocId::DEFAULT);

    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host);

    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t> &args = {},
        std::chrono::milliseconds timeout = timeout::FIRMWARE_STARTUP_TIMEOUT);

    DeviceProtocol *get_device_protocol();

    PcieInterface *get_pcie_interface();

    JtagInterface *get_jtag_interface();

    RemoteInterface *get_remote_interface();

    FirmwareTelemetryReader *get_firmware_telemetry_reader() const;

    FirmwareInfoProvider *get_firmware_info_provider() const;

    RemoteCommunication *get_remote_communication();
    ARCH get_arch() const;

    ChipInfo get_chip_info();

    const SocDescriptor &get_soc_descriptor() const;

    FirmwareBundleVersion get_firmware_version();

    bool get_noc_translation_enabled() const;

    uint64_t get_board_id() const;

    uint8_t get_asic_location() const;

    BoardType get_board_type() const;

    ArchitectureImplementation *get_architecture_implementation();

    bool is_remote() const;

    double get_asic_temperature() const;

    uint32_t get_clock_freq() const;

    uint32_t get_max_clock_freq() const;

    uint32_t get_min_clock_freq() const;

    uint64_t get_refclk_counter(NocId noc = NocId::DEFAULT) const;

    int get_communication_device_id() const;

    IODeviceType get_communication_device_type() const;

    int get_numa_node() const;

    EthTrainingStatus get_eth_core_training_status(CoreCoord eth_core);

    void set_power_state(PowerState state);

    void set_clock_state(PowerState state);

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
