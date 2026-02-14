// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {
class RtlSimulationTTDevice : public TTDevice {
public:
    RtlSimulationTTDevice(const std::filesystem::path &simulator_directory, const SocDescriptor &soc_descriptor);
    ~RtlSimulationTTDevice();

    static std::unique_ptr<RtlSimulationTTDevice> create(const std::filesystem::path &simulator_directory);

    void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void send_tensix_risc_reset(tt_xy_pair translated_core, bool deassert);

    SocDescriptor *get_soc_descriptor() { return &soc_descriptor_; }

    void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    bool wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) override;
    std::chrono::milliseconds wait_eth_core_training(
        const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) override;
    uint32_t get_clock() override;
    uint32_t get_min_clock_freq() override;
    bool get_noc_translation_enabled() override;

private:
    void start_host_communication();
    void close_device();

    std::mutex device_lock;
    SimulationHost host;
    std::filesystem::path simulator_directory_;
    SocDescriptor soc_descriptor_;
};
}  // namespace tt::umd
