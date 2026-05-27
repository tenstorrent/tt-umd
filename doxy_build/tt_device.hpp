// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "components.hpp"

namespace tt::umd {

class TTDeviceModel;

class TTDevice final {
public:
    enum class HangAction { THROW, RETURN };
    enum class PowerState { BUSY, IDLE };

    TTDevice(std::unique_ptr<TTDeviceModel> model);
    ~TTDevice();

    void init_device(std::chrono::milliseconds timeout_ms = timeout::FIRMWARE_STARTUP_TIMEOUT);

    // Data I/O.
    void read_data(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT_NOC);
    void write_data(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT_NOC);
    void read_ctrl(void *dst, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT_NOC);
    void write_ctrl(const void *src, CoreCoord core, uint64_t addr, size_t size, NocId noc = NocId::DEFAULT_NOC);
    void write_to_core_range(
        const void *src,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        uint64_t addr,
        NocId noc = NocId::DEFAULT_NOC);
    void write_to_core_range(const void *src, size_t size, uint64_t addr, NocId noc = NocId::DEFAULT_NOC);

    // DMA.
    void dma_write_to_core_range(const void *src, uint64_t dst_addr, size_t size, CoreCoord core);
    void dma_read(void *dst, uint64_t src_addr, size_t size, CoreCoord core);
    void dma_write(const void *src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end);
    void dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core);
    void dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, CoreCoord core);
    void dma_write_to_core_range_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end);

    // Firmware.
    void wait_firmware_startup(std::chrono::milliseconds timeout_ms = timeout::FIRMWARE_STARTUP_TIMEOUT);
    std::chrono::milliseconds wait_eth_core_training(
        CoreCoord eth_core, std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);
    void wait_dram_channel_training(
        uint32_t dram_channel, std::chrono::milliseconds timeout_ms = timeout::DRAM_TRAINING_TIMEOUT);
    void wait_for_non_mmio_flush();
    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t> &args = {},
        std::chrono::milliseconds timeout = timeout::FIRMWARE_MESSAGE_TIMEOUT);
    EthTrainingStatus read_eth_core_training_status(CoreCoord eth_core);
    void set_power_state(PowerState state);
    void set_clock_state(PowerState state);

    // Hang detection.
    bool is_bus_hung(uint32_t data_read = HANG_READ_VALUE, HangAction action = HangAction::THROW);
    bool is_noc_hung(NocId noc, HangAction action = HangAction::THROW);

    // RISC reset.
    RiscType get_risc_reset_state(CoreCoord core);
    void assert_risc_reset(CoreCoord core, RiscType selected_riscs);
    void deassert_risc_reset(CoreCoord core, RiscType selected_riscs, bool staggered_start = false);

    // IoWindow.
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host);

    // Component getters.
    DeviceProtocol *get_device_protocol();
    PcieInterface *get_pcie_interface();
    JtagInterface *get_jtag_interface();
    RemoteInterface *get_remote_interface();
    FirmwareTelemetryReader *get_firmware_telemetry_reader() const;
    FirmwareInfoProvider *get_firmware_info_provider() const;
    RemoteCommunication *get_remote_communication();
    ArchitectureImplementation *get_architecture_implementation();

    // Identity.
    tt::ARCH get_arch() const;
    ChipInfo get_chip_info();
    const SocDescriptor &get_soc_descriptor() const;
    FirmwareBundleVersion get_firmware_version();
    bool get_noc_translation_enabled() const;
    uint64_t get_board_id() const;
    uint8_t get_asic_location() const;
    BoardType get_board_type() const;
    bool is_remote() const;
    int get_communication_device_id() const;
    IODeviceType get_communication_device_type() const;

    // Clock and thermal.
    double get_asic_temperature() const;
    uint32_t get_clock_freq() const;
    uint32_t get_max_clock_freq() const;
    uint32_t get_min_clock_freq() const;
    uint64_t get_refclk_counter() const;
    int get_numa_node() const;

    static void set_sigbus_safe_handler(bool set_safe_handler);

private:
    tt_xy_pair translate(CoreCoord core) const;

    std::unique_ptr<TTDeviceModel> model_;

    // Mandatory.
    std::unique_ptr<DeviceProtocol> device_protocol_ = nullptr;
    std::unique_ptr<DeviceFirmware> device_firmware_ = nullptr;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_ = nullptr;

    // Optional.
    std::unique_ptr<DeviceController> device_controller_ = nullptr;
    std::unique_ptr<HangDetector> hang_detector_ = nullptr;
    std::unique_ptr<FirmwareTelemetryReader> firmware_telemetry_reader_ = nullptr;
    std::unique_ptr<FirmwareInfoProvider> firmware_info_provider_ = nullptr;

    PcieInterface *pcie_interface_ = nullptr;
    DmaInterface *dma_interface_ = nullptr;
    JtagInterface *jtag_interface_ = nullptr;
    RemoteInterface *remote_interface_ = nullptr;

    std::optional<SocDescriptor> soc_descriptor_;

    tt::ARCH arch_ = tt::ARCH::Invalid;
    IODeviceType device_type_ = IODeviceType::UNDEFINED;
    int device_id_ = -1;
    bool is_remote_ = false;
};

}  // namespace tt::umd
