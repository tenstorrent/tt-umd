// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device.hpp"

#include <cstring>
#include <map>
#include <unordered_map>

#include "tt-umd/arc/arc_messenger.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"

namespace tt::umd {

namespace {
// Mock device memory: per-core byte-addressable storage. Reads return 0 for
// addresses that were never written so that callers like LocalChip::set_membar_flag
// (which write a flag then poll for it) terminate.
std::unordered_map<tt_xy_pair, std::map<uint64_t, uint8_t>>& mock_memory() {
    static std::unordered_map<tt_xy_pair, std::map<uint64_t, uint8_t>> memory;
    return memory;
}
}  // namespace

// Concrete TTDevice for the mock environment. All pure-virtual methods have
// trivial no-op / zero implementations; these code paths are never exercised.
class MockImpl : public TTDevice {
public:
    MockImpl() {
        arch = tt::ARCH::QUASAR;
        communication_device_type_ = IODeviceType::UNDEFINED;
        communication_device_id_ = 0;
        architecture_impl_ = architecture_implementation::create(arch);
        arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    }

    void read_from_arc_apb(void*, uint64_t, [[maybe_unused]] size_t) override {}

    void write_to_arc_apb(const void*, uint64_t, [[maybe_unused]] size_t) override {}

    void read_from_arc_csm(void*, uint64_t, [[maybe_unused]] size_t) override {}

    void write_to_arc_csm(const void*, uint64_t, [[maybe_unused]] size_t) override {}

    void wait_arc_core_start(std::chrono::milliseconds) override {}

    std::chrono::milliseconds wait_eth_core_training(tt_xy_pair, std::chrono::milliseconds) override {
        return std::chrono::milliseconds{0};
    }

    uint32_t get_clock() override { return 0; }

    uint32_t get_min_clock_freq() override { return 0; }

    bool get_noc_translation_enabled() override { return false; }

    EthTrainingStatus read_eth_core_training_status(tt_xy_pair) override { return EthTrainingStatus::NOT_CONNECTED; }

protected:
    void retrain_dram_core(uint32_t) override {}
};

// TTDevice non-virtual method stubs.

PCIDevice* TTDevice::get_pci_device() { return nullptr; }

architecture_implementation* TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

RemoteCommunication* TTDevice::get_remote_communication() { return nullptr; }

IODeviceType TTDevice::get_communication_device_type() const { return communication_device_type_; }

void TTDevice::init_tt_device(std::chrono::milliseconds) {}

tt::ARCH TTDevice::get_arch() { return arch; }

void TTDevice::wait_dram_channel_training(const uint32_t, const std::chrono::milliseconds) {}

uint32_t TTDevice::get_risc_reset_state(tt_xy_pair) { return 0; }

ArcMessenger* TTDevice::get_arc_messenger() const { return arc_messenger_.get(); }

uint32_t TTDevice::get_max_clock_freq() { return 0; }

double TTDevice::get_asic_temperature() { return 0.0; }

void TTDevice::dma_d2h(void*, uint32_t, size_t) {}

void TTDevice::dma_d2h_zero_copy(void*, uint32_t, size_t) {}

void TTDevice::dma_h2d(uint32_t, const void*, size_t) {}

void TTDevice::configure_iatu_region(size_t, uint64_t, size_t) {}

void TTDevice::set_power_state(bool) {}

void TTDevice::read_from_device(void* dst, tt_xy_pair core, uint64_t addr, uint32_t size) {
    auto* bytes = static_cast<uint8_t*>(dst);
    auto core_it = mock_memory().find(core);
    if (core_it == mock_memory().end()) {
        std::memset(dst, 0, size);
        return;
    }
    const auto& core_memory = core_it->second;
    for (uint32_t i = 0; i < size; i++) {
        auto byte_it = core_memory.find(addr + i);
        bytes[i] = (byte_it == core_memory.end()) ? uint8_t{0} : byte_it->second;
    }
}

void TTDevice::write_to_device(const void* src, tt_xy_pair core, uint64_t addr, uint32_t size) {
    const auto* bytes = static_cast<const uint8_t*>(src);
    auto& core_memory = mock_memory()[core];
    for (uint32_t i = 0; i < size; i++) {
        core_memory[addr + i] = bytes[i];
    }
}

void TTDevice::assert_risc_reset(tt_xy_pair, const RiscType) {}

void TTDevice::deassert_risc_reset(tt_xy_pair, const RiscType, bool) {}

void TTDevice::send_tensix_risc_reset(tt_xy_pair, const TensixSoftResetOptions&) {}

void TTDevice::send_tensix_risc_reset(const TensixSoftResetOptions&) {}

ChipInfo TTDevice::get_chip_info() { return ChipInfo{}; }

void TTDevice::dma_multicast_write(void*, size_t, tt_xy_pair, tt_xy_pair, uint64_t) {}

TTDevice::TTDevice() = default;

void TTDevice::noc_multicast_write(void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    for (size_t x = core_start.x; x <= core_end.x; x++) {
        for (size_t y = core_start.y; y <= core_end.y; y++) {
            write_to_device(src, tt_xy_pair{x, y}, addr, static_cast<uint32_t>(size));
        }
    }
}

void TTDevice::dma_read_from_device(void*, size_t, tt_xy_pair, uint64_t) {}

void TTDevice::dma_write_to_device(const void*, size_t, tt_xy_pair, uint64_t) {}

void TTDevice::dma_h2d_zero_copy(uint32_t, const void*, size_t) {}

void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_noc_hung(NocId, HangAction) { return false; }

void TTDevice::set_risc_reset_state(tt_xy_pair, const uint32_t) {}

BoardType TTDevice::get_board_type() { return BoardType::UNKNOWN; }

uint64_t TTDevice::get_board_id() { return 0; }

uint32_t TTDevice::bar_read32(uint32_t) { return 0; }

FirmwareBundleVersion TTDevice::get_firmware_version() { return FirmwareBundleVersion{}; }

void TTDevice::bar_write32(uint32_t, uint32_t) {}

bool TTDevice::is_pcie_hung(uint32_t, HangAction) { return false; }

int TTDevice::get_communication_device_id() const { return communication_device_id_; }

FirmwareInfoProvider* TTDevice::get_firmware_info_provider() const { return firmware_info_provider.get(); }

ArcTelemetryReader* TTDevice::get_arc_telemetry_reader() const { return telemetry.get(); }

bool TTDevice::is_remote() { return is_remote_tt_device; }

std::unique_ptr<TTDevice> TTDevice::create(int, IODeviceType, bool) { return std::make_unique<MockImpl>(); }

std::unique_ptr<TTDevice> TTDevice::create(std::unique_ptr<RemoteCommunication>) {
    return std::make_unique<MockImpl>();
}

// ArcMessenger non-virtual send_message overload.
uint32_t ArcMessenger::send_message(
    const uint32_t msg_code, const std::vector<uint32_t>& args, const std::chrono::milliseconds timeout_ms) {
    std::vector<uint32_t> return_values;
    return send_message(msg_code, return_values, args, timeout_ms);
}

// In the mock build only the grendel (QUASAR) arch impl is compiled in, so
// hand out a grendel_implementation regardless of the requested architecture.
std::unique_ptr<architecture_implementation> architecture_implementation::create(tt::ARCH) {
    return std::make_unique<grendel_implementation>();
}

}  // namespace tt::umd
