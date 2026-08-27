// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

class DmaInterface;
class FirmwareTelemetryReader;
class HangDetector;
class JtagInterface;
class PCIDevice;
class PcieInterface;
class RemoteInterface;
class SocArchDescriptor;

/**
 * Composition root for a single device's hardware-facing components.
 *
 * A model is built before the TTDevice that consumes it and handed over at construction, so
 * device-specific choices are made here rather than spread across TTDevice subclasses.
 *
 * Models are specialized by architecture, and by backend for simulation. The transport is
 * deliberately not a subclass axis: it selects a constructor instead, so one architecture model
 * serves every transport that architecture supports.
 *
 * This is a pure interface: it holds no state and provides no constructor, so each concrete model
 * declares whatever it needs to answer with. Required components are pure virtual; optional ones
 * return nullptr, and a concrete model overrides only those it actually provides.
 *
 * TODO: temporary note - components are declared here as they are decoupled from TTDevice, so only
 * some of the required ones appear so far. Remove this note once the migration is done.
 */
class TTDeviceModel {
public:
    virtual ~TTDeviceModel() = default;

    // TODO: temporary - not part of the Base API. Answered by
    // get_architecture_impl()->get_architecture() once ArchitectureImplementation moves here.
    virtual tt::ARCH get_arch() const = 0;

    // Identifies the device within its transport: the PCI device number for PCIe, the JLink id for
    // JTAG. A remote device reports the identity of the local device it is reached through.
    // TODO: temporary - not part of the Base API. Answered by get_device_protocol()->get_mmio_id()
    // once DeviceProtocol moves here.
    virtual int get_communication_device_id() const = 0;

    // Required components.
    virtual DeviceProtocol *get_device_protocol() = 0;

    // The model resolves this from what the caller supplied, or from its architecture's constants.
    virtual SocArchDescriptor *get_soc_arch_descriptor() = 0;

    // Optional components.
    virtual HangDetector *get_hang_detector() { return nullptr; }

    virtual FirmwareTelemetryReader *get_firmware_telemetry_reader() { return nullptr; }

    // Optional transport interfaces.
    virtual PcieInterface *get_pcie_interface() { return nullptr; }

    virtual DmaInterface *get_dma_interface() { return nullptr; }

    virtual JtagInterface *get_jtag_interface() { return nullptr; }

    virtual RemoteInterface *get_remote_interface() { return nullptr; }

    // TODO: temporary - SocDescriptor shares ownership of the architecture descriptor, so TTDevice
    // needs the shared_ptr rather than the raw pointer the Base API exposes above. Delete once
    // SocDescriptor's ownership model is revisited.
    virtual std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() = 0;

    // TODO: temporary - delete along with TTDevice::get_pci_device() once callers go through
    // PcieInterface only.
    virtual PCIDevice *get_pci_device() { return nullptr; }
};

}  // namespace tt::umd
