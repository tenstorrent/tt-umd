// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/types/communication_protocol.hpp"

namespace tt::umd {

class RemoteCommunication;
class SocArchDescriptor;
class SocDescriptor;
class TTDevice;

/**
 * Creates a TTDevice for a locally attached device.
 *
 * The composition root for a device: probes the architecture over the requested transport, builds
 * the matching TTDeviceModel with that transport, and injects it into the matching TTDevice.
 * TTDevice itself carries no knowledge of how it is assembled.
 *
 * @param device_number The device identifier/index to connect to, specific to the I/O device interface.
 * @param device_type The type of I/O device interface to use. (default: PCIe)
 * @param use_safe_api Flag to enable safe I/O API that can recover from SIGBUS errors.
 *                     Available only for PCIe I/O device type. (default: false)
 * @param soc_arch_descriptor Shared pointer to the SoC architecture descriptor.
 *                            If nullptr, a default descriptor will be used. (default: nullptr)
 * @return std::unique_ptr<TTDevice> The created device.
 * @throws May throw exceptions if device creation fails or device_number is invalid.
 */
std::unique_ptr<TTDevice> create_tt_device(
    int device_number,
    IODeviceType device_type = IODeviceType::PCIe,
    bool use_safe_api = false,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor = nullptr);

/**
 * Creates a TTDevice for a remote device reached over ethernet through a local gateway.
 */
std::unique_ptr<TTDevice> create_tt_device(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor = nullptr);

#ifdef TT_UMD_BUILD_SIMULATION
/**
 * Creates a TTDevice for a simulated remote chip.
 *
 * A remote TTDevice is normally initialized over ARC (init_tt_device), which constructs its
 * SocDescriptor. Simulated remote chips have no ARC to probe, so the caller supplies the full
 * descriptor directly. This is a dedicated factory (compiled in only for simulation builds) rather
 * than an overload of the silicon create_tt_device() above, so the simulation-only flow stays fully
 * separated from the silicon path.
 * TODO: temporary - remove once ttsim provides a mocked ARC that can serve the SocDescriptor like
 * silicon does.
 */
std::unique_ptr<TTDevice> create_simulation_remote_tt_device(
    std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor &soc_descriptor);
#endif  // TT_UMD_BUILD_SIMULATION

}  // namespace tt::umd
