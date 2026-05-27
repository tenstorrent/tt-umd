// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "stub_device_model.hpp"

namespace tt::umd {
namespace {

class StubDeviceInfo : public DeviceInfo {
public:
    tt::ARCH get_arch() const override { return tt::ARCH::BLACKHOLE; }

    IODeviceType get_device_type() const override { return IODeviceType::PCIE; }

    int get_device_id() const override { return 0; }

    bool is_remote() const override { return false; }
};

class StubDeviceProtocol : public DeviceProtocol {
public:
    void write_data(const void *, tt_xy_pair, uint64_t, size_t, NocId) override {}

    void read_data(void *, tt_xy_pair, uint64_t, size_t, NocId) override {}

    void write_ctrl(const void *, tt_xy_pair, uint64_t, size_t, NocId) override {}

    void read_ctrl(void *, tt_xy_pair, uint64_t, size_t, NocId) override {}

    bool write_to_core_range(const void *, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t, NocId) override { return true; }
};

class StubDeviceFirmware : public DeviceFirmware {
public:
    void wait_firmware_startup(std::chrono::milliseconds) override {}

    DeviceCommandResult send_device_command(
        uint32_t, const std::vector<uint32_t> &, std::chrono::milliseconds) override {
        return {};
    }

    bool get_noc_translation_enabled() override { return false; }

    tt_xy_pair get_firmware_noc_coord() const override { return {0, 0}; }

    void set_power_state(uint32_t) override {}
};

class StubDeviceController : public DeviceController {
public:
    ChipInfo get_chip_info() override { return {}; }

    std::chrono::milliseconds wait_eth_core_training(CoreCoord, std::chrono::milliseconds t) override { return t; }

    void wait_dram_channel_training(uint32_t, std::chrono::milliseconds) override {}

    EthTrainingStatus get_eth_core_training_status(CoreCoord) override { return EthTrainingStatus::NOT_CONNECTED; }

    void set_clock_state(uint32_t) override {}
};

class StubArchImpl : public ArchitectureImplementation {
public:
    tt::ARCH get_architecture() const override { return tt::ARCH::BLACKHOLE; }

    uint32_t get_min_clock_freq() const override { return 200; }

    uint64_t get_arc_reset_unit_refclk_high_offset() const override { return 0x100; }

    uint64_t get_arc_reset_unit_refclk_low_offset() const override { return 0x104; }

    uint64_t get_tensix_soft_reset_addr() const override { return 0x200; }

    uint32_t get_soft_reset_reg_value(RiscType) const override { return 0xFF; }

    uint32_t get_soft_reset_staggered_start() const override { return 0x1; }

    RiscType get_soft_reset_risc_type(uint32_t) const override { return RiscType::ALL; }
};

}  // namespace

std::unique_ptr<DeviceInfo> StubDeviceModel::create_device_info() { return std::make_unique<StubDeviceInfo>(); }

std::unique_ptr<DeviceProtocol> StubDeviceModel::create_device_protocol() {
    return std::make_unique<StubDeviceProtocol>();
}

std::unique_ptr<DeviceFirmware> StubDeviceModel::create_device_firmware() {
    return std::make_unique<StubDeviceFirmware>();
}

std::unique_ptr<ArchitectureImplementation> StubDeviceModel::create_architecture_impl() {
    return std::make_unique<StubArchImpl>();
}

std::unique_ptr<IoWindow> StubDeviceModel::create_io_window(TargetIoWindowConfig, HostIoWindowConfig) {
    return nullptr;
}

std::unique_ptr<DeviceController> StubDeviceModel::create_device_controller() {
    return std::make_unique<StubDeviceController>();
}

}  // namespace tt::umd
