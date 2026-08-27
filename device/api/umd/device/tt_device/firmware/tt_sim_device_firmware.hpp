// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

#include "umd/device/tt_device/firmware/simulation_device_firmware.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Firmware implementation for the TTSim simulation backend.
 *
 * Overrides the operations TTSimTTDevice handles differently from the common simulation behavior.
 */
class TTSimDeviceFirmware : public SimulationDeviceFirmware {
public:
    /**
     * @brief Builds firmware for a TTSim device of the given architecture.
     * @param arch Architecture being simulated; get_chip_info() reports harvesting per architecture.
     */
    explicit TTSimDeviceFirmware(tt::ARCH arch);

    void init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    ChipInfo get_chip_info(NocId noc_id = NocId::DEFAULT_NOC) override;

    bool wait_eth_core_training(
        tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    EthTrainingStatus get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    tt::ARCH arch_ = tt::ARCH::Invalid;
};

}  // namespace tt::umd
