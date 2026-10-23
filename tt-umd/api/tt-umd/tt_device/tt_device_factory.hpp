// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "tt-umd/types/communication_protocol.hpp"

namespace tt::umd {

class RemoteCommunication;
class SocArchDescriptor;
class SocDescriptor;
class TTDevice;

/**
 * @brief Creates a TTDevice for a locally attached device.
 *
 * Probes the architecture over the requested transport, builds the model for it, and hands that
 * model to the device.
 *
 * @param device_number Device identifier, specific to the I/O device interface.
 * @param device_type Transport to reach the device over.
 * @param use_safe_api Enables the I/O API that recovers from SIGBUS. PCIe only.
 * @param soc_arch_descriptor Architecture descriptor to build with; the architecture's own is used
 * when none is given.
 */
std::unique_ptr<TTDevice> create_tt_device(
    int device_number,
    IODeviceType device_type = IODeviceType::PCIe,
    bool use_safe_api = false,
    const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor = nullptr);

/**
 * @brief Creates a TTDevice for a device reached over ethernet through a local one.
 *
 * @param remote_communication Communication with the remote device, through its gateway.
 * @param soc_arch_descriptor Architecture descriptor to build with; the architecture's own is used
 * when none is given.
 */
std::unique_ptr<TTDevice> create_tt_device(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor = nullptr);

#ifdef TT_UMD_BUILD_SIMULATION
/**
 * @brief Creates a TTDevice for a simulated remote device, which supplies its own SoC descriptor.
 *
 * A remote device is normally initialized over ARC, which builds its SocDescriptor. A simulated
 * remote chip has no ARC to probe, so the caller supplies the descriptor.
 *
 * @param remote_communication Communication with the simulated remote device.
 * @param soc_descriptor The descriptor the device is built with; its architecture must match the
 * remote device's.
 *
 * TODO: temporary - remove once ttsim provides a mocked ARC that can serve the SocDescriptor like
 * silicon does.
 */
std::unique_ptr<TTDevice> create_simulation_remote_tt_device(
    std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor& soc_descriptor);
#endif  // TT_UMD_BUILD_SIMULATION

}  // namespace tt::umd
