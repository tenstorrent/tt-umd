// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/mmio/rtl_simulation_mmio_device_io.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace tt::umd {

RTLSimulationMMIODeviceIO::RTLSimulationMMIODeviceIO(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    size_t size,
    uint64_t base_address,
    const tlb_data& config)
    : simulator_directory_(simulator_directory),
      soc_descriptor_(soc_descriptor),
      base_address_(base_address),
      config_(config),
      window_size_(size) {
    // Initialize RTL simulation host if directory is provided
    if (!simulator_directory_.empty()) {
        start_host_communication();
    }
}

RTLSimulationMMIODeviceIO::~RTLSimulationMMIODeviceIO() {
    close_device();
}

void RTLSimulationMMIODeviceIO::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));

    if (!host_communication_started_) {
        throw std::runtime_error("RTL simulation host communication not started");
    }

    std::lock_guard<std::mutex> lock(device_lock_);

    // Convert offset to memory address and write through simulation host
    uint64_t addr = base_address_ + offset;

    // For RTL simulation, we need to determine which core this maps to
    // This is a simplified approach - real implementation would use proper address decoding
    tt_xy_pair core = {0, 0};  // Default to core (0,0) for direct memory access

    rtl_write_to_device(&value, core, addr, sizeof(uint32_t));
}

uint32_t RTLSimulationMMIODeviceIO::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));

    if (!host_communication_started_) {
        throw std::runtime_error("RTL simulation host communication not started");
    }

    std::lock_guard<std::mutex> lock(device_lock_);

    uint32_t value = 0;
    uint64_t addr = base_address_ + offset;

    // For RTL simulation, we need to determine which core this maps to
    tt_xy_pair core = {0, 0};  // Default to core (0,0) for direct memory access

    rtl_read_from_device(&value, core, addr, sizeof(uint32_t));
    return value;
}

void RTLSimulationMMIODeviceIO::write_register(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);

    if (!host_communication_started_) {
        throw std::runtime_error("RTL simulation host communication not started");
    }

    std::lock_guard<std::mutex> lock(device_lock_);

    uint64_t addr = base_address_ + offset;
    tt_xy_pair core = {0, 0};  // Default core for register access

    rtl_write_to_device(data, core, addr, size);
}

void RTLSimulationMMIODeviceIO::read_register(uint64_t offset, void* data, size_t size) {
    validate(offset, size);

    if (!host_communication_started_) {
        throw std::runtime_error("RTL simulation host communication not started");
    }

    std::lock_guard<std::mutex> lock(device_lock_);

    uint64_t addr = base_address_ + offset;
    tt_xy_pair core = {0, 0};  // Default core for register access

    rtl_read_from_device(data, core, addr, size);
}

void RTLSimulationMMIODeviceIO::write_block(uint64_t offset, const void* data, size_t size) {
    write_register(offset, data, size);
}

void RTLSimulationMMIODeviceIO::read_block(uint64_t offset, void* data, size_t size) {
    read_register(offset, data, size);
}

void RTLSimulationMMIODeviceIO::read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    if (!host_communication_started_) {
        throw std::runtime_error("RTL simulation host communication not started");
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    rtl_read_from_device(mem_ptr, core, addr, size);
}

void RTLSimulationMMIODeviceIO::write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    if (!host_communication_started_) {
        throw std::runtime_error("RTL simulation host communication not started");
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    rtl_write_to_device(mem_ptr, core, addr, size);
}

void RTLSimulationMMIODeviceIO::noc_multicast_write_reconfigure(
    void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    if (!host_communication_started_) {
        throw std::runtime_error("RTL simulation host communication not started");
    }

    // In RTL simulation, multicast is simulated by writing to each core in the range
    std::lock_guard<std::mutex> lock(device_lock_);

    for (uint32_t x = core_start.x; x <= core_end.x; ++x) {
        for (uint32_t y = core_start.y; y <= core_end.y; ++y) {
            rtl_write_to_device(dst, {x, y}, addr, size);
        }
    }
}

size_t RTLSimulationMMIODeviceIO::get_size() const {
    return window_size_;
}

void RTLSimulationMMIODeviceIO::configure(const tlb_data& new_config) {
    std::lock_guard<std::mutex> lock(device_lock_);
    config_ = new_config;
    // In RTL simulation, configuration changes are stored but don't affect simulation behavior directly
}

uint64_t RTLSimulationMMIODeviceIO::get_base_address() const {
    return base_address_;
}

void RTLSimulationMMIODeviceIO::start_host_communication() {
    if (host_communication_started_) {
        return;
    }

    try {
        // Initialize the simulation host (similar to RtlSimulationTTDevice)
        // This would typically involve setting up communication channels
        // with the RTL simulator
        host_communication_started_ = true;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to start RTL simulation host communication: " + std::string(e.what()));
    }
}

void RTLSimulationMMIODeviceIO::close_device() {
    std::lock_guard<std::mutex> lock(device_lock_);

    if (host_communication_started_) {
        // Close simulation host communication
        host_communication_started_ = false;
    }
}

void RTLSimulationMMIODeviceIO::validate(uint64_t offset, size_t size) const {
    if (offset + size > window_size_) {
        throw std::runtime_error("RTLSimulationMMIODeviceIO: Access beyond window boundary");
    }
}

uint64_t RTLSimulationMMIODeviceIO::translate_address_for_rtl(tt_xy_pair core, uint64_t addr) const {
    // Address translation for RTL simulation
    // This would typically involve converting core coordinates and local address
    // to the appropriate RTL simulator address format
    return addr + (static_cast<uint64_t>(core.x) << 20) + (static_cast<uint64_t>(core.y) << 12);
}

void RTLSimulationMMIODeviceIO::rtl_read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    // Perform read operation through RTL simulation host
    // This would interface with the SimulationHost to perform the actual read

    // Placeholder implementation - real version would use SimulationHost methods
    // uint64_t translated_addr = translate_address_for_rtl(core, addr);
    // host_.read_from_device(mem_ptr, core, translated_addr, size);

    // For now, zero out the memory to avoid undefined behavior
    std::memset(mem_ptr, 0, size);

    // Suppress unused parameter warnings
    (void)core;
    (void)addr;
}

void RTLSimulationMMIODeviceIO::rtl_write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    // Perform write operation through RTL simulation host
    // This would interface with the SimulationHost to perform the actual write

    // Placeholder implementation - real version would use SimulationHost methods
    // uint64_t translated_addr = translate_address_for_rtl(core, addr);
    // host_.write_to_device(mem_ptr, core, translated_addr, size);

    // For now, this is a no-op in the placeholder implementation

    // Suppress unused parameter warnings
    (void)mem_ptr;
    (void)core;
    (void)addr;
    (void)size;
}

}  // namespace tt::umd