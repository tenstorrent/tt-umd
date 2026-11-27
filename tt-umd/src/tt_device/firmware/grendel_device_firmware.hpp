// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device/firmware/quasar_smc_message_queue.hpp"

namespace tt::umd {

/**
 * Firmware lifecycle for Grendel, where only messaging is available yet.
 *
 * Commands go to the management firmware on the package's Keraunos I/O chiplet SMC through the
 * message queue it publishes. No firmware message is defined yet for anything else this interface
 * covers, so every other call is the corresponding no-op rather than a throw: the device is
 * reachable and its memory can be read and written, and refusing to construct over missing
 * firmware support would make that unreachable too.
 *
 * As firmware support appears on the device, calls move out of here one at a time.
 */
class GrendelDeviceFirmware : public DeviceFirmware {
public:
    /**
     * @param device_protocol Issues the firmware accesses; its mmio id names the device's message
     *        lock. Non-owning; must outlive this object.
     * @param kmd_lock_available Whether that lock, which this device shares with other processes,
     *        can be taken through the device's KMD lock table. Pass false where no KMD device backs
     *        the protocol, and the lock falls back to shared memory alone.
     */
    explicit GrendelDeviceFirmware(DeviceProtocol* device_protocol, bool kmd_lock_available = true);

    void init_firmware(
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    /**
     * Sends the command to the Keraunos I/O chiplet's SMC and waits for its response, holding the
     * device's firmware message lock so other threads and processes cannot interleave.
     *
     * The response is not decoded: exit_code is response word 0 exactly as the firmware wrote it,
     * and return_values holds response words 1 to 7. noc_id is ignored, because the queue is
     * reachable by system physical address only.
     *
     * @throws error::RuntimeError as QuasarSmcMessageQueue::send_message does.
     */
    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t>& args,
        std::chrono::milliseconds timeout,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    void set_clock_state(
        [[maybe_unused]] ClockState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    void set_power_state(
        [[maybe_unused]] PowerState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    /** No firmware programs a translation table, so coordinates arrive the way the caller wrote them. */
    bool get_noc_translation_enabled([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override { return false; }

    ChipInfo get_chip_info([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override { return ChipInfo{}; }

    tt_xy_pair get_firmware_noc_coord([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const override {
        return tt_xy_pair{};
    }

    bool wait_eth_core_training(
        [[maybe_unused]] tt_xy_pair eth_core,
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return true;
    }

    EthTrainingStatus get_eth_core_training_status(
        [[maybe_unused]] tt_xy_pair eth_core, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return EthTrainingStatus::SUCCESS;
    }

    bool wait_dram_channel_training(
        [[maybe_unused]] uint32_t dram_channel,
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return true;
    }

    std::optional<uint64_t> get_runtime_telemetry_buffer_address(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return std::nullopt;
    }

    std::optional<uint32_t> get_runtime_telemetry_buffer_size(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return std::nullopt;
    }

    /**
     * Unlike the calls above, there is no no-op that would be honest here: zero is a valid reading
     * of a stopped counter, and a caller cannot tell it apart from one this device cannot take.
     */
    uint64_t get_refclk_counter([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        UMD_THROW(error::RuntimeError, "The refclk counter is not available on this device.");
    }

private:
    int device_id_;
    QuasarSmcMessageQueue message_queue_;
};

}  // namespace tt::umd
