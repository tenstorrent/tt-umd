// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

class SimulationSysmemManager;
class SimulationTlbAllocator;
class TlbWindow;

// Common base class for the simulation TTDevice backends (TTSimTTDevice and RtlSimulationTTDevice).
// It is introduced as an intermediary in the class hierarchy and owns the state that is shared by
// both backends. The behavior that operates on this state still lives in the derived classes for now
// and will be migrated into this base incrementally.
//
// The backend communicator is intentionally NOT owned here: TTSimCommunicator and RtlSimCommunicator
// are unrelated `final` classes with no common base, so each derived device keeps its own
// concretely-typed communicator.
class SimulationTTDevice : public TTDevice {
protected:
    SimulationTTDevice(
        const std::filesystem::path& simulator_directory, std::unique_ptr<SimulationSysmemManager> sysmem_manager) :
        simulator_directory_(simulator_directory), sysmem_manager_(std::move(sysmem_manager)) {}

    // Client-mode constructor: the device does not own a local simulator, so it has no simulator
    // directory or sysmem manager -- those live on the remote host reached over the socket.
    SimulationTTDevice() = default;

    std::recursive_mutex device_lock;
    std::filesystem::path simulator_directory_;
    std::unique_ptr<SimulationSysmemManager> sysmem_manager_;
    std::shared_ptr<SimulationTlbAllocator> tlb_allocator_;
    std::unique_ptr<TlbWindow> cached_tlb_window_ = nullptr;
};

}  // namespace tt::umd
