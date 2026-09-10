// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/blackhole_tt_device.hpp"

#include <fmt/format.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "tracy.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/architecture_registers.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/blackhole_hang_detector.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

BlackholeTTDevice::BlackholeTTDevice(std::unique_ptr<TTDeviceModel> model) : TTDevice(std::move(model)) {}

uint32_t BlackholeTTDevice::get_clock() {
    if (get_firmware_telemetry_reader()->is_entry_available(TelemetryTag::AICLK)) {
        return get_firmware_telemetry_reader()->read_entry(TelemetryTag::AICLK);
    }

    UMD_THROW(error::RuntimeError, "AICLK telemetry not available for Blackhole device.");
}

}  // namespace tt::umd
