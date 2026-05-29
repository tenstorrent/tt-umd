// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include "tt_architecture_implementation_doxy.hpp"
#include "tt_device_firmware_doxy.hpp"
#include "tt_device_protocol_doxy.hpp"
#include "tt_dma_interface_doxy.hpp"
#include "tt_firmware_info_provider_doxy.hpp"
#include "tt_firmware_telemetry_reader_doxy.hpp"
#include "tt_hang_detector_doxy.hpp"
#include "tt_io_window_doxy.hpp"
#include "tt_jtag_interface_doxy.hpp"
#include "tt_pcie_interface_doxy.hpp"
#include "tt_remote_interface_doxy.hpp"
#include "tt_soc_arch_descriptor_doxy.hpp"

namespace tt::umd {

/**
 * @defgroup tt_device_model TTDeviceModel
 * @{
 *
 * @brief Provides and assembles the hardware-facing components for a single device.
 *
 * TTDeviceModel is the composition root that TTDevice receives at construction.
 * Each concrete model (PCIe, JTAG, mock, etc.) wires together the components
 * appropriate for its transport and architecture:
 *
 * | Component | Role |
 * |-----------|------|
 * | @ref DeviceProtocol | Transport-level I/O (data and control paths) |
 * | @ref DeviceFirmware | Firmware lifecycle, commands, training, power/clock |
 * | @ref ArchitectureImplementation | Architecture-specific constants and register encodings |
 * | @ref SocArchDescriptor | Static chip topology (grid, core locations, memory sizes) |
 * | @ref IoWindow | Memory-mapped window into device address space |
 * | @ref HangDetector | Bus and NOC probes |
 * | @ref FirmwareTelemetryReader | Raw telemetry reads from firmware memory |
 * | @ref FirmwareInfoProvider | Versioned telemetry interpretation |
 *
 * Not every transport supports every component. Optional components return
 * nullptr by default; concrete models override only what they provide.
 *
 */

/**
 * @brief Composition root for a single device's hardware-facing components.
 *
 * Concrete subclasses (one per transport/architecture combination) create and
 * own the component instances. TTDevice consumes the model and delegates
 * hardware interactions through the component interfaces.
 */
class TTDeviceModel {
public:
    virtual ~TTDeviceModel() = default;

    /** @name Required Components */
    /** @{ */

    /**
     * @brief Returns the transport-level I/O interface.
     */
    virtual DeviceProtocol* get_device_protocol() = 0;

    /**
     * @brief Returns the firmware lifecycle and command interface.
     */
    virtual DeviceFirmware* get_device_firmware() = 0;

    /**
     * @brief Returns architecture-specific constants and register encodings.
     */
    virtual ArchitectureImplementation* get_architecture_impl() = 0;

    /**
     * @brief Returns the static chip topology descriptor.
     */
    virtual SocArchDescriptor* get_soc_arch_descriptor() = 0;

    /**
     * @brief Creates an I/O window mapping host memory to device address space.
     * @param target Device-side target (core, address, NOC).
     * @param host Host-side properties (caching, size).
     * @return std::unique_ptr<IoWindow> Exclusively owned window handle.
     */
    virtual std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) = 0;

    /** @} */

    /** @name Optional Components */
    /** @{ */

    /**
     * @brief Returns the hang detector, or nullptr if not supported.
     */
    virtual HangDetector* get_hang_detector() { return nullptr; }

    /**
     * @brief Returns the raw telemetry reader, or nullptr if not supported.
     */
    virtual FirmwareTelemetryReader* get_firmware_telemetry_reader() { return nullptr; }

    /**
     * @brief Returns the versioned telemetry provider, or nullptr if not supported.
     */
    virtual FirmwareInfoProvider* get_firmware_info_provider() { return nullptr; }

    /** @} */

    /** @name Optional Transport Interfaces */
    /** @{ */

    /**
     * @brief Returns the PCIe-specific interface, or nullptr if not PCIe-connected.
     */
    virtual PcieInterface* get_pcie_interface() { return nullptr; }

    /**
     * @brief Returns the DMA transfer interface, or nullptr if DMA is not available.
     */
    virtual DmaInterface* get_dma_interface() { return nullptr; }

    /**
     * @brief Returns the JTAG-specific interface, or nullptr if not JTAG-connected.
     */
    virtual JtagInterface* get_jtag_interface() { return nullptr; }

    /**
     * @brief Returns the remote transport interface, or nullptr if not remotely connected.
     */
    virtual RemoteInterface* get_remote_interface() { return nullptr; }

    /** @} */
};

/** @} */  // end of tt_device_model group

}  // namespace tt::umd
