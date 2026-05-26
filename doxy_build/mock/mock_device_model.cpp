// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "mock_device_model.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

namespace tt::umd {
namespace {

uint64_t encode_addr(tt_xy_pair core, uint64_t addr) {
    return (static_cast<uint64_t>(core.x) << 48) | (static_cast<uint64_t>(core.y) << 40) | addr;
}

// --- MockPcieProtocol: DeviceProtocol + PcieInterface + DmaInterface ---

class MockPcieProtocol : public DeviceProtocol, public PcieInterface, public DmaInterface {
    std::map<uint64_t, std::vector<uint8_t>> memory_;
    std::map<uint32_t, uint32_t> bar_regs_;

public:
    // DeviceProtocol.
    void write_data(const void *src, tt_xy_pair core, uint64_t addr, size_t size, NocId) override {
        uint64_t key = encode_addr(core, addr);
        auto &buf = memory_[key];
        buf.resize(size);
        std::memcpy(buf.data(), src, size);
    }

    void read_data(void *dst, tt_xy_pair core, uint64_t addr, size_t size, NocId) override {
        uint64_t key = encode_addr(core, addr);
        auto it = memory_.find(key);
        if (it != memory_.end()) {
            size_t n = std::min(size, it->second.size());
            std::memcpy(dst, it->second.data(), n);
            if (n < size) {
                std::memset(static_cast<uint8_t *>(dst) + n, 0, size - n);
            }
        } else {
            std::memset(dst, 0, size);
        }
    }

    void write_ctrl(const void *src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc) override {
        write_data(src, core, addr, size, noc);
    }

    void read_ctrl(void *dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc) override {
        read_data(dst, core, addr, size, noc);
    }

    bool write_to_core_range(
        const void *src, tt_xy_pair start, tt_xy_pair end, uint64_t addr, uint32_t size, NocId noc) override {
        for (size_t x = start.x; x <= end.x; ++x) {
            for (size_t y = start.y; y <= end.y; ++y) {
                write_data(src, {x, y}, addr, size, noc);
            }
        }
        return true;
    }

    // PcieInterface.
    void bar_write32(uint32_t addr, uint32_t data) override { bar_regs_[addr] = data; }

    uint32_t bar_read32(uint32_t addr) override { return bar_regs_[addr]; }

    int get_numa_node() const override { return 0; }

    // DmaInterface.
    bool dma_write(const void *src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc) override {
        write_data(src, core, dst_addr, size, noc);
        return true;
    }

    bool dma_read(void *dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc) override {
        read_data(dst, core, src_addr, size, noc);
        return true;
    }

    bool dma_multicast_write(
        const void *src, uint64_t dst_addr, size_t size, tt_xy_pair start, tt_xy_pair end, NocId noc) override {
        write_to_core_range(src, start, end, dst_addr, size, noc);
        return true;
    }

    void dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc) override {
        write_data(reinterpret_cast<const void *>(src_iova), core, dst_addr, size, noc);
    }

    void dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc) override {
        read_data(reinterpret_cast<void *>(dst_iova), core, src_addr, size, noc);
    }

    void dma_multicast_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair start, tt_xy_pair end, NocId noc) override {
        const void *src = reinterpret_cast<const void *>(src_iova);
        write_to_core_range(src, start, end, dst_addr, size, noc);
    }
};

// --- MockJtagProtocol: DeviceProtocol only ---

class MockJtagProtocol : public DeviceProtocol {
    std::map<uint64_t, std::vector<uint8_t>> memory_;

public:
    void write_data(const void *src, tt_xy_pair core, uint64_t addr, size_t size, NocId) override {
        uint64_t key = encode_addr(core, addr);
        auto &buf = memory_[key];
        buf.resize(size);
        std::memcpy(buf.data(), src, size);
    }

    void read_data(void *dst, tt_xy_pair core, uint64_t addr, size_t size, NocId) override {
        uint64_t key = encode_addr(core, addr);
        auto it = memory_.find(key);
        if (it != memory_.end()) {
            size_t n = std::min(size, it->second.size());
            std::memcpy(dst, it->second.data(), n);
            if (n < size) {
                std::memset(static_cast<uint8_t *>(dst) + n, 0, size - n);
            }
        } else {
            std::memset(dst, 0, size);
        }
    }

    void write_ctrl(const void *src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc) override {
        write_data(src, core, addr, size, noc);
    }

    void read_ctrl(void *dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc) override {
        read_data(dst, core, addr, size, noc);
    }

    bool write_to_core_range(const void *, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t, NocId) override { return true; }
};

// --- Shared mock components ---

class MockDeviceFirmware : public DeviceFirmware {
public:
    void wait_firmware_startup(std::chrono::milliseconds) override {}

    std::chrono::milliseconds wait_eth_core_training(CoreCoord, std::chrono::milliseconds t) override { return t; }

    void wait_dram_channel_training(uint32_t, std::chrono::milliseconds) override {}

    void wait_for_non_mmio_flush() override {}

    DeviceCommandResult send_device_command(
        uint32_t, const std::vector<uint32_t> &, std::chrono::milliseconds) override {
        return {0, {0x42}};
    }

    EthTrainingStatus read_eth_core_training_status(CoreCoord) override { return EthTrainingStatus::TRAINED; }

    ChipInfo get_chip_info() override { return {true, 0xAA5500000001ULL, BoardType::P150, 0, {0, 0, 0}}; }

    FirmwareBundleVersion get_firmware_version() override { return {6, 12, 0}; }

    uint32_t get_clock_freq() override { return 1000; }

    bool get_noc_translation_enabled() override { return true; }

    tt_xy_pair get_arc_core() const override { return {8, 0}; }

    void set_power_state(uint32_t) override {}

    void set_clock_state(uint32_t) override {}
};

class MockArchImpl : public ArchitectureImplementation {
public:
    tt::ARCH get_architecture() const override { return tt::ARCH::BLACKHOLE; }

    uint32_t get_min_clock_freq() const override { return 200; }

    uint64_t get_arc_reset_unit_refclk_high_offset() const override { return 0x80030100; }

    uint64_t get_arc_reset_unit_refclk_low_offset() const override { return 0x80030104; }

    uint64_t get_tensix_soft_reset_addr() const override { return 0xFFB121B0; }

    uint32_t get_soft_reset_reg_value(RiscType) const override { return 0x7FF; }

    uint32_t get_soft_reset_staggered_start() const override { return 0x10; }

    RiscType get_soft_reset_risc_type(uint32_t val) const override { return val == 0 ? RiscType::NONE : RiscType::ALL; }
};

class MockDeviceInfo : public DeviceInfo {
    IODeviceType type_;
    int id_;

public:
    MockDeviceInfo(IODeviceType type, int id) : type_(type), id_(id) {}

    tt::ARCH get_arch() const override { return tt::ARCH::BLACKHOLE; }

    IODeviceType get_device_type() const override { return type_; }

    int get_device_id() const override { return id_; }

    bool is_remote() const override { return false; }
};

// --- PCIe-only optional components ---

class MockHangDetector : public HangDetector {
protected:
    uint32_t read_hang_check_reg_via_bar() override { return 0x12345678; }

    uint32_t read_hang_check_reg_via_noc(NocId) override { return 0x12345678; }
};

class MockFirmwareTelemetryReader : public FirmwareTelemetryReader {
public:
    uint32_t read_entry(uint8_t tag) override {
        switch (tag) {
            case 0:
                return 1000;
            case 1:
                return 45;
            case 2:
                return 850;
            default:
                return 0;
        }
    }

    bool is_entry_available(uint8_t tag) override { return tag < 3; }
};

class MockFirmwareInfoProvider : public FirmwareInfoProvider {
public:
    std::optional<uint64_t> get_board_id() const override { return 0xAA5500000001ULL; }

    std::optional<uint8_t> get_asic_location() const override { return 0; }

    std::optional<double> get_asic_temperature() const override { return 42.5; }

    std::optional<uint32_t> get_aiclk() const override { return 1000; }

    std::optional<uint32_t> get_max_clock_freq() const override { return 1200; }

    FirmwareBundleVersion get_firmware_version() const override { return {6, 12, 0}; }
};

}  // namespace

// --- MockPcieModel ---

std::unique_ptr<DeviceInfo> MockPcieModel::create_device_info() {
    return std::make_unique<MockDeviceInfo>(IODeviceType::PCIE, device_id_);
}

std::unique_ptr<DeviceProtocol> MockPcieModel::create_device_protocol() { return std::make_unique<MockPcieProtocol>(); }

std::unique_ptr<DeviceFirmware> MockPcieModel::create_device_firmware() {
    return std::make_unique<MockDeviceFirmware>();
}

std::unique_ptr<ArchitectureImplementation> MockPcieModel::create_architecture_impl() {
    return std::make_unique<MockArchImpl>();
}

std::unique_ptr<HangDetector> MockPcieModel::create_hang_detector() { return std::make_unique<MockHangDetector>(); }

std::unique_ptr<FirmwareTelemetryReader> MockPcieModel::create_firmware_telemetry_reader() {
    return std::make_unique<MockFirmwareTelemetryReader>();
}

std::unique_ptr<FirmwareInfoProvider> MockPcieModel::create_firmware_info_provider() {
    return std::make_unique<MockFirmwareInfoProvider>();
}

// --- MockJtagModel ---

std::unique_ptr<DeviceInfo> MockJtagModel::create_device_info() {
    return std::make_unique<MockDeviceInfo>(IODeviceType::JTAG, device_id_);
}

std::unique_ptr<DeviceProtocol> MockJtagModel::create_device_protocol() { return std::make_unique<MockJtagProtocol>(); }

std::unique_ptr<DeviceFirmware> MockJtagModel::create_device_firmware() {
    return std::make_unique<MockDeviceFirmware>();
}

std::unique_ptr<ArchitectureImplementation> MockJtagModel::create_architecture_impl() {
    return std::make_unique<MockArchImpl>();
}

// --- Factory ---

std::unique_ptr<TTDevice> create_mock_device(IODeviceType type, int device_id) {
    std::unique_ptr<TTDeviceModel> model;
    switch (type) {
        case IODeviceType::PCIE:
            model = std::make_unique<MockPcieModel>(device_id);
            break;
        case IODeviceType::JTAG:
            model = std::make_unique<MockJtagModel>(device_id);
            break;
        default:
            throw std::runtime_error("Unsupported IO device type");
    }
    return std::make_unique<TTDevice>(std::move(model));
}

}  // namespace tt::umd
