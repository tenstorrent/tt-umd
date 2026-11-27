// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "tt-umd/types/communication_protocol.hpp"

namespace tt::umd {

class RemoteCommunication;
class SocArchDescriptor;
class TTDevice;

/**
 * @brief Creates a TTDevice for a locally attached device.
 *
 * Probes the architecture over the requested transport, builds the model for it, and hands that
 * model to the device.
 *
 * @param device_number The PCIe device number.
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

}  // namespace tt::umd
