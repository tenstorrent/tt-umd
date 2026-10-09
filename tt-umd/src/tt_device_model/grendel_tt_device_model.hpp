// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "tt-umd/tt_device/protocol/kmd_scalar_noc_access.hpp"
#include "tt-umd/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

class ArchitectureImplementation;
class SocArchDescriptor;

/**
 * The components a Grendel device runs on.
 *
 * Quasar is reached through the kernel driver's scalar accesses rather than through a mapped
 * translation window, so this model supplies a protocol built on those and declares the rest of
 * the transport interfaces absent. It is deliberately the smallest model that TTDevice can be
 * driven through: no BAR access, no DMA engine, no hang detection and no firmware, because the
 * driver exposes none of them for this architecture yet.
 */
class GrendelTTDeviceModel : public TTDeviceModel {
public:
    /**
     * @param access How an access reaches the device.
     * @param mmio_id Identifies this device and its protocol instance.
     * @param soc_arch_descriptor Caller's descriptor, or nullptr for the architecture's own.
     */
    GrendelTTDeviceModel(
        std::shared_ptr<KmdScalarNocAccess> access,
        int mmio_id,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    ~GrendelTTDeviceModel() override;

    /**
     * A window served from the same scalar accesses the protocol uses, because the TLB path
     * TTDevice takes otherwise allocates a hardware mapping and this architecture exposes none.
     */
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) override;

    DeviceProtocol *get_device_protocol() override;
    DeviceFirmware *get_device_firmware() override;
    ArchitectureImplementation *get_architecture_impl() override;
    SocArchDescriptor *get_soc_arch_descriptor() override;

    /**
     * Quasar's NOC carries no coordinate: the ATT decodes a flat address into a destination tile
     * and a tile-local offset, so a caller's coordinate has to be folded into the address before
     * the access is issued. Offering the resolver is what asks TTDevice to do that.
     */
    att::EndpointResolver *get_endpoint_resolver() override;
    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;

private:
    std::shared_ptr<KmdScalarNocAccess> access_;
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::unique_ptr<DeviceProtocol> protocol_;
    std::unique_ptr<DeviceFirmware> device_firmware_;

    // The qsr.s1 programming, which is the Quasar instance's own; see grendel_qsr1_att_map.hpp.
    // Built once because inverting the endpoint tables into a coordinate lookup is not free.
    att::EndpointResolver endpoint_resolver_{att::GRENDEL_QSR1_MAP};
};

}  // namespace tt::umd
