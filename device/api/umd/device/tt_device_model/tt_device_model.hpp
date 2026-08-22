// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/types/arch.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace tt::umd {

class DmaInterface;
class FirmwareTelemetryReader;
class HangDetector;
class JtagInterface;
class PcieInterface;
class RemoteInterface;

/**
 * Composition root for a single device's hardware-facing components.
 *
 * A model is built before the TTDevice that consumes it and handed over at construction, so
 * device-specific choices are made here rather than spread across TTDevice subclasses.
 *
 * Models are specialized by architecture, and for simulation by backend. The transport is
 * deliberately not a subclass axis: transport-specific components (the DeviceProtocol and its
 * interface facets, system memory, I/O windows) are built from the transport object and injected, so
 * one architecture model serves every transport that architecture supports.
 *
 * Optional components return nullptr; concrete models override only what they provide.
 *
 * TODO: temporary note - components move into the model one at a time as they are decoupled from
 * TTDevice, so for now a model carries nothing but device identity and no component getter is
 * overridden anywhere. That is also why the concrete models currently differ only in the
 * architecture they report: each is already its own class so it has somewhere to wire its
 * architecture-specific components -- architecture implementation, hang detector, device firmware --
 * as those arrive. Remove this note once the migration is done.
 */
class TTDeviceModel {
public:
    virtual ~TTDeviceModel() = default;

    tt::ARCH get_arch() const;

    IODeviceType get_communication_device_type() const;

    // Identifies the device within its transport: the PCI device number for PCIe, the JLink id for
    // JTAG. A remote device reports the identity of the local device it is reached through.
    int get_communication_device_id() const;

    virtual HangDetector *get_hang_detector() { return nullptr; }

    virtual FirmwareTelemetryReader *get_firmware_telemetry_reader() { return nullptr; }

    virtual PcieInterface *get_pcie_interface() { return nullptr; }

    virtual DmaInterface *get_dma_interface() { return nullptr; }

    virtual JtagInterface *get_jtag_interface() { return nullptr; }

    virtual RemoteInterface *get_remote_interface() { return nullptr; }

protected:
    TTDeviceModel(tt::ARCH arch, IODeviceType communication_device_type, int communication_device_id);

private:
    tt::ARCH arch_;
    IODeviceType communication_device_type_;
    int communication_device_id_;
};

}  // namespace tt::umd
