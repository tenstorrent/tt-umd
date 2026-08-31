// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/firmware/wormhole_arc_window.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/eth_training_status.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {
class DeviceProtocol;
class FirmwareInfoProvider;
class FirmwareTelemetryReader;
class JtagInterface;
class PcieInterface;
class RemoteInterface;
class ArchitectureImplementation;

/**
 * @brief Wormhole management firmware implementation.
 *
 * The interfaces this talks to are forward declared above, so this header stays free of the protocol
 * headers; they are included in the .cpp.
 */
class WormholeDeviceFirmware : public DeviceFirmware {
public:
    /**
     * @param remote_interface Non-null only for a device reached over ethernet through a gateway. It
     * selects the remote route for ARC accesses and flushes ethernet writes before a command is
     * triggered.
     */
    WormholeDeviceFirmware(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        RemoteInterface* remote_interface,
        ArchitectureImplementation* architecture_impl,
        // Slots owned by the caller. Null until init_firmware() fills them: they cannot be built
        // before the firmware is up, and this class is what brings it up.
        std::unique_ptr<FirmwareTelemetryReader>& firmware_telemetry_reader,
        std::unique_ptr<FirmwareInfoProvider>& firmware_info_provider);

    void init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t>& args,
        std::chrono::milliseconds timeout,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    ChipInfo get_chip_info(NocId noc_id = NocId::DEFAULT_NOC) override;

    bool get_noc_translation_enabled(NocId noc_id = NocId::DEFAULT_NOC) override;

    tt_xy_pair get_firmware_noc_coord(NocId noc_id = NocId::DEFAULT_NOC) const override;

    bool wait_eth_core_training(
        tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    EthTrainingStatus get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id = NocId::DEFAULT_NOC) override;

    bool wait_dram_channel_training(
        uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    DramTrainingStatus get_dram_channel_training_status(
        uint32_t dram_channel, NocId noc_id = NocId::DEFAULT_NOC) override;

    void set_power_state(PowerState state, NocId noc_id = NocId::DEFAULT_NOC) override;

    void set_clock_state(ClockState state, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    /**
     * @brief Blocks until the management firmware reports it has booted.
     *
     * Split out from init_firmware() to mark the seam where this becomes its own API: waiting for
     * the firmware and building the components that depend on it are two jobs, and callers such as
     * warm reset only want the first. Once they are separate entry points, init_firmware()'s
     * idempotence guard goes away with them.
     *
     * @param timeout_ms How long to wait for the firmware to report ready.
     * @param noc_id NOC to route the status reads over.
     * @throws error::FirmwareStartupError if the firmware does not come up within the timeout.
     */
    void wait_firmware_ready(std::chrono::milliseconds timeout_ms, NocId noc_id);

    IODeviceType get_io_device_type() const;

    // Polls AICLK until it is within tolerance of target_aiclk. Logs and returns if it does not
    // settle, rather than throwing.
    void wait_for_aiclk_value(
        uint32_t target_aiclk, NocId noc_id, std::chrono::milliseconds timeout_ms = timeout::AICLK_TIMEOUT);

    // Thin wrappers that resolve the ARC core for noc_id and hand the access to arc_apb_/arc_csm_.
    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    void read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    // All non-owning; they belong to the component that owns this firmware class and must outlive it.
    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    JtagInterface* jtag_interface_ = nullptr;
    RemoteInterface* remote_interface_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;
    // References to the owner's slots, not owned here.
    std::unique_ptr<FirmwareTelemetryReader>& firmware_telemetry_reader_;
    std::unique_ptr<FirmwareInfoProvider>& firmware_info_provider_;

    // Names the ARC message mutex; taken from the protocol so it identifies this device, not the
    // silicon model. See DeviceProtocol::get_mmio_id().
    const int device_id_ = 0;

    // ARC core coordinate per NOC. Fixed for Wormhole, so resolved once in the constructor.
    tt_xy_pair arc_core_noc0_;
    tt_xy_pair arc_core_noc1_;

    WormholeArcWindow arc_apb_;
    WormholeArcWindow arc_csm_;

    // Serializes ARC messages against other processes driving the same device, exactly as
    // ArcMessenger does for the path this replaces.
    LockManager lock_manager_;
};

}  // namespace tt::umd
