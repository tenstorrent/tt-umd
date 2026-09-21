// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/tt_device/protocol/scalar_noc_access.hpp"
#include "umd/device/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

class ArchitectureImplementation;
class SocArchDescriptor;

/**
 * The components a Quasar device runs on.
 *
 * Quasar is reached through the kernel driver's scalar accesses rather than through a mapped
 * translation window, so this model supplies a protocol built on those and declares the rest of
 * the transport interfaces absent. It is deliberately the smallest model that TTDevice can be
 * driven through: no BAR access, no DMA engine, no hang detection and no firmware, because the
 * driver exposes none of them for this architecture yet.
 */
class QuasarTTDeviceModel : public TTDeviceModel {
public:
    /**
     * @param access How an access reaches the device.
     * @param mmio_id Identifies this device and its protocol instance.
     * @param soc_arch_descriptor Caller's descriptor, or nullptr for the architecture's own.
     */
    QuasarTTDeviceModel(
        std::unique_ptr<ScalarNocAccess> access,
        int mmio_id,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    ~QuasarTTDeviceModel() override;

    DeviceProtocol *get_device_protocol() override;
    DeviceFirmware *get_device_firmware() override;
    ArchitectureImplementation *get_architecture_impl() override;
    SocArchDescriptor *get_soc_arch_descriptor() override;
    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;

private:
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::unique_ptr<DeviceProtocol> protocol_;
    std::unique_ptr<DeviceFirmware> device_firmware_;
};

}  // namespace tt::umd
