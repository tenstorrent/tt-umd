// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulator_paths.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <tt-logger/tt-logger.hpp>

#include "tt-kmd-lib/pci_ids.h"
#include "tt-umd/simulation/tt_sim_communicator.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path) {
    const std::string soc_descriptor_path = find_simulator_soc_descriptor_path(simulator_path);
    if (!soc_descriptor_path.empty()) {
        return SocDescriptor::get_arch_from_soc_descriptor_path(soc_descriptor_path);
    }
    UMD_ASSERT(
        simulator_path.extension() == ".so",
        error::RuntimeError,
        fmt::format(
            "{} not found: an RTL simulator declares its architecture only through its SoC descriptor.",
            get_simulator_soc_descriptor_path(simulator_path)));

    const uint16_t device_id = TTSimCommunicator::read_pci_device_id(simulator_path);
    tt::ARCH arch = tt::ARCH::Invalid;
    switch (device_id) {
        case TT_WORMHOLE_PCI_DEVICE_ID:
            arch = tt::ARCH::WORMHOLE_B0;
            break;
        case TT_BLACKHOLE_PCI_DEVICE_ID:
            arch = tt::ARCH::BLACKHOLE;
            break;
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Simulator {} reports PCI device id {:#x}, which has no built-in SoC descriptor. Place its "
                    "soc_descriptor.yaml at {}.",
                    simulator_path.string(),
                    device_id,
                    get_simulator_soc_descriptor_path(simulator_path)));
    }
    log_debug(
        LogUMD, "Simulator {} reports PCI device id {:#x}: {}", simulator_path.string(), device_id, arch_to_str(arch));
    return arch;
}

}  // namespace tt::umd
