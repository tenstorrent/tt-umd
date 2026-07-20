// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {
class JtagDevice;
class PCIDevice;
class RemoteCommunication;
enum class IODeviceType;

class WormholeTTDevice : public TTDevice {
public:
    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    void wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) override;

    uint32_t get_clock() override;

    uint32_t get_min_clock_freq() override;

    void set_clock_state(DevicePowerState state) override;

    bool get_noc_translation_enabled() override;

    void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    ChipInfo get_chip_info() override;

    std::chrono::milliseconds wait_eth_core_training(
        CoreCoord eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) override;

    EthTrainingStatus read_eth_core_training_status(CoreCoord eth_core) override;

    ~WormholeTTDevice() override = default;

protected:
    WormholeTTDevice(
        std::unique_ptr<PCIDevice> pci_device,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor,
        bool use_safe_api);
    WormholeTTDevice(
        std::unique_ptr<JtagDevice> jtag_device,
        uint8_t jlink_id,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    WormholeTTDevice(
        std::unique_ptr<RemoteCommunication> remote_communication,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);

    void retrain_dram_core(const uint32_t dram_channel) override;

    void set_arc_coordinate() override;

private:
    // Builds the ARC message (with the common prefix) that requests the given clock state.
    uint32_t get_power_state_arc_msg(DevicePowerState state);

    friend std::unique_ptr<TTDevice> TTDevice::create(
        int device_number,
        IODeviceType device_type,
        bool use_safe_api,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    friend std::unique_ptr<TTDevice> TTDevice::create(
        std::unique_ptr<RemoteCommunication> remote_communication,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
#ifdef TT_UMD_BUILD_SIMULATION
    friend std::unique_ptr<TTDevice> TTDevice::create_simulation_remote(
        std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor &soc_descriptor);
#endif  // TT_UMD_BUILD_SIMULATION
};
}  // namespace tt::umd
