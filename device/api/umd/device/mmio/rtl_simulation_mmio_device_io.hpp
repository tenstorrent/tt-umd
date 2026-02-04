// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "umd/device/mmio/mmio_device_io.hpp"
#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt::umd {

/**
 * RTL Simulation implementation of MMIODeviceIO that interfaces with RTL simulator.
 * Similar to RtlSimulationTTDevice but focused on MMIO operations.
 */
class RTLSimulationMMIODeviceIO : public MMIODeviceIO {
public:
    /**
     * Constructor for RTL Simulation MMIO device.
     *
     * @param simulator_directory Path to the RTL simulator directory.
     * @param soc_descriptor SoC descriptor for the device.
     * @param size Size of the memory window.
     * @param base_address Base address for the window.
     * @param config Initial TLB configuration.
     */
    RTLSimulationMMIODeviceIO(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        size_t size,
        uint64_t base_address = 0,
        const tlb_data& config = {});

    ~RTLSimulationMMIODeviceIO() override;

    void write32(uint64_t offset, uint32_t value) override;

    uint32_t read32(uint64_t offset) override;

    void write_register(uint64_t offset, const void* data, size_t size) override;

    void read_register(uint64_t offset, void* data, size_t size) override;

    void write_block(uint64_t offset, const void* data, size_t size) override;

    void read_block(uint64_t offset, void* data, size_t size) override;

    void read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) override;

    void write_block_reconfigure(
        const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) override;

    void noc_multicast_write_reconfigure(
        void* dst,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        uint64_t ordering = tlb_data::Strict) override;

    size_t get_size() const override;

    void configure(const tlb_data& new_config) override;

    uint64_t get_base_address() const override;

    /**
     * Start the RTL simulation host communication.
     */
    void start_host_communication();

    /**
     * Close the RTL simulation device.
     */
    void close_device();

protected:
    void validate(uint64_t offset, size_t size) const override;

private:
    std::mutex device_lock_;
    SimulationHost host_;
    std::filesystem::path simulator_directory_;
    SocDescriptor soc_descriptor_;
    uint64_t base_address_;
    tlb_data config_;
    size_t window_size_;
    bool host_communication_started_ = false;

    /**
     * Convert core coordinates and address for RTL simulation.
     */
    uint64_t translate_address_for_rtl(tt_xy_pair core, uint64_t addr) const;

    /**
     * Perform read operation through RTL simulation host.
     */
    void rtl_read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    /**
     * Perform write operation through RTL simulation host.
     */
    void rtl_write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);
};

}  // namespace tt::umd