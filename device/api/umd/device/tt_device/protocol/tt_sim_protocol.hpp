// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/power_state.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class TTSimCommunicator;
class SimulationTTDevice;
class SimulationTTDeviceModel;

/**
 * @brief Transport for a chip modelled by a TTSim simulator image.
 *
 * A simulator is reached in-process rather than over a host bus, but it models the same PCIe
 * surface silicon exposes -- config space, BAR windows, and NOC access to the chip's tiles. Serving
 * that surface as a DeviceProtocol is what lets the silicon firmware stack read a simulated device:
 * ArcTelemetryReader, FirmwareInfoProviderImplementation and the per-architecture DeviceFirmware
 * implementations are all built from these interfaces and need no knowledge of what is behind them.
 *
 * PcieInterface is served alongside DeviceProtocol because the architecture firmwares reach some
 * registers by BAR rather than over the NOC -- Blackhole reads NOC translation state that way. A
 * model supplying this protocol must therefore also supply a HangDetector, since TTDevice runs its
 * bus-hang check whenever a PcieInterface is present.
 *
 * Non-owning: the communicator belongs to the device that owns this protocol and must outlive it.
 */
class TTSimProtocol : public DeviceProtocol, public PcieInterface {
public:
    // The model that owns this protocol, so that attaching a working transport can hand it the
    // architecture's firmware -- which cannot exist any earlier, since it reads the device as it is
    // constructed.
    explicit TTSimProtocol(SimulationTTDeviceModel* model);

    // The device that owns this protocol, wired in once it exists. A model builds its components
    // before the TTDevice that consumes them, so this cannot be a constructor argument; the device
    // registers itself and its communicator as the last step of coming up.
    void attach(SimulationTTDevice* device, TTSimCommunicator* communicator, int chip_id);

    TTSimCommunicator* get_communicator() const { return communicator_; }

    // --- DeviceProtocol ---
    //
    // A simulator has no separate ordered-register transport: every access reaches it through the
    // same entry points, clocked synchronously from the calling thread, so ordering is already
    // guaranteed and the ctrl variants delegate to the data ones.
    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;

    // No hardware multicast is modelled, so callers fall back to unicast.
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;

    int get_mmio_id() override;

    // --- PcieInterface ---
    void bar_write32(uint32_t addr, uint32_t data) override;
    uint32_t bar_read32(uint32_t addr) override;
    int get_numa_node() const override;
    void set_power_state(PowerState state) override;
    int export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) override;
    void set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) override;

private:
    // BAR0's physical base, read from the simulator's config space on first use. The simulator
    // decodes the whole window, so a BAR offset is added to this base directly.
    uint64_t bar0_base();

    SimulationTTDeviceModel* model_ = nullptr;
    SimulationTTDevice* device_ = nullptr;
    TTSimCommunicator* communicator_ = nullptr;
    int chip_id_ = 0;
    uint64_t bar0_base_ = 0;
    bool bar0_base_read_ = false;
};

}  // namespace tt::umd
