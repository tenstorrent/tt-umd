// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/kmd_power_hold.hpp"

#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/kmd_versions.hpp"

namespace tt::umd {

/* static */ std::unique_ptr<KmdPowerHold> KmdPowerHold::create(uint64_t asic_id) {
    tt_device_t *device_handle = nullptr;
    // O_APPEND makes the fd power-aware: it requests nothing until it sets a power state explicitly.
    int ret = tt_device_open_by_asic_id(asic_id, &device_handle, O_APPEND);
    if (ret == -ENODEV) {
        return nullptr;
    }
    if (ret != 0) {
        log_warning(
            LogUMD,
            "Failed to open the PCIe device of ASIC {:#x} to keep it powered: {}",
            asic_id,
            std::strerror(-ret));
        return nullptr;
    }
    auto hold = std::unique_ptr<KmdPowerHold>(new KmdPowerHold(device_handle));

    // Older drivers have no power policy and never power the chip down, so holding the fd open is enough.
    if (PCIDevice::read_kmd_version() < KMD_POWER_STATE) {
        return hold;
    }
    // Same domains a busy PCIe client requests.
    constexpr uint16_t busy_flags =
        TT_POWER_FLAG_MRISC_PHY_WAKEUP | TT_POWER_FLAG_L2CPU_ENABLE | TT_POWER_FLAG_TENSIX_ENABLE;
    ret = tt_device_set_power_state(device_handle, busy_flags);
    if (ret != 0) {
        log_warning(LogUMD, "Failed to request power for ASIC {:#x}: {}", asic_id, std::strerror(-ret));
        return nullptr;
    }
    log_debug(LogUMD, "Holding the PCIe device of ASIC {:#x} open to keep it powered.", asic_id);
    return hold;
}

KmdPowerHold::KmdPowerHold(tt_device_t *device_handle) : device_handle_(device_handle) {}

KmdPowerHold::~KmdPowerHold() { tt_device_close(device_handle_); }

}  // namespace tt::umd
