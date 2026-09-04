// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"

namespace tt::umd {
class DeviceFirmware;

class ArchitectureImplementation;
class DmaInterface;
class FirmwareInfoProvider;
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

    // Required components.
    virtual DeviceProtocol *get_device_protocol() = 0;

    /**
     * @brief The device's management firmware component.
     *
     * Created and owned by the concrete model, like every other component: the model knows its
     * architecture and backend statically, so no dispatch is involved in picking the implementation.
     */
    virtual DeviceFirmware *get_device_firmware() = 0;

    virtual ArchitectureImplementation *get_architecture_impl() = 0;

    // The model resolves this from what the caller supplied, or from its architecture's constants.
    virtual SocArchDescriptor *get_soc_arch_descriptor() = 0;

    // Optional components.
    virtual HangDetector *get_hang_detector() { return nullptr; }

    // Lent from the firmware component, which owns them because they read state the firmware
    // publishes: null until DeviceFirmware::init_firmware() has run. Simulation models keep the
    // default - a simulated device has no firmware-published state to read.
    virtual FirmwareTelemetryReader *get_firmware_telemetry_reader() { return nullptr; }

    virtual FirmwareInfoProvider *get_firmware_info_provider() { return nullptr; }

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

    // TODO: temporary - not part of the Base API. Raw ARC APB window access, served here so
    // TTDevice::read_from_arc_apb()/write_to_arc_apb() need no per-arch TTDevice subclass. The
    // silicon models route these to their firmware component's APB window; the default throws,
    // since no other model has an APB window to serve them from. Deleted once the remaining raw
    // APB callers (the SPI device and the refclk counter read) move onto components of their own.
    virtual void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    virtual void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);
};

}  // namespace tt::umd
