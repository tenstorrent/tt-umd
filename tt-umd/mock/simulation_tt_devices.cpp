// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// HACK: This file exists only so that tt-umd-workload links cleanly when built
// against the mock library. The simulation TTDevice classes (TTSimTTDevice and
// RtlSimulationTTDevice) are referenced from tt-umd-workload's simulation chip
// implementations, which forces every virtual override on these classes to
// appear in the vtable, which forces every override to have a definition.
//
// The bodies are intentionally trivial (no-op / zero / default). The constructors
// throw because actually instantiating one of these in the mock build is
// nonsensical; the per-method stubs exist purely to satisfy the linker.
//
// This whole file should be deleted once tt-umd-workload no longer references
// the simulation TTDevices (or once the simulation TTDevices are moved out of
// the api so the mock doesn't need to mirror their full vtable).

#include <stdexcept>

#include "tt-umd/simulation/rtl_simulation_tt_device.hpp"
#include "tt-umd/simulation/tt_sim_tt_device.hpp"

namespace tt::umd {

// The TTSim/RtlSim communicator types are forward-declared in the public headers
// and defined in src/simulation/. Provide empty stubs so the unique_ptr<Communicator>
// member destructors in the simulation TTDevices can be instantiated.
class TTSimCommunicator final {};

class RtlSimCommunicator final {};

namespace {
[[noreturn]] void unsupported(const char* what) {
    throw std::runtime_error(std::string(what) + " is not supported in the mock build");
}
}  // namespace

// ---------------------------------------------------------------------------
// TTSimTTDevice
// ---------------------------------------------------------------------------

TTSimTTDevice::TTSimTTDevice(const std::filesystem::path&, SocDescriptor, ChipId, bool, int) {
    unsupported("TTSimTTDevice");
}

TTSimTTDevice::~TTSimTTDevice() = default;

TLBManager* TTSimTTDevice::get_tlb_manager() { return nullptr; }

void TTSimTTDevice::read_from_device(void*, tt_xy_pair, uint64_t, uint32_t) {}

void TTSimTTDevice::write_to_device(const void*, tt_xy_pair, uint64_t, uint32_t) {}

void TTSimTTDevice::dma_d2h(void*, uint32_t, size_t) {}

void TTSimTTDevice::dma_d2h_zero_copy(void*, uint32_t, size_t) {}

void TTSimTTDevice::dma_h2d(uint32_t, const void*, size_t) {}

void TTSimTTDevice::dma_h2d_zero_copy(uint32_t, const void*, size_t) {}

void TTSimTTDevice::read_from_arc_apb(void*, uint64_t, size_t) {}

void TTSimTTDevice::write_to_arc_apb(const void*, uint64_t, size_t) {}

void TTSimTTDevice::read_from_arc_csm(void*, uint64_t, size_t) {}

void TTSimTTDevice::write_to_arc_csm(const void*, uint64_t, size_t) {}

void TTSimTTDevice::wait_arc_core_start(std::chrono::milliseconds) {}

std::chrono::milliseconds TTSimTTDevice::wait_eth_core_training(tt_xy_pair, std::chrono::milliseconds) {
    return std::chrono::milliseconds{0};
}

EthTrainingStatus TTSimTTDevice::read_eth_core_training_status(tt_xy_pair) { return EthTrainingStatus::NOT_CONNECTED; }

uint32_t TTSimTTDevice::get_clock() { return 0; }

uint32_t TTSimTTDevice::get_min_clock_freq() { return 0; }

bool TTSimTTDevice::get_noc_translation_enabled() { return false; }

void TTSimTTDevice::dma_multicast_write(void*, size_t, tt_xy_pair, tt_xy_pair, uint64_t) {}

void TTSimTTDevice::send_tensix_risc_reset(tt_xy_pair, const TensixSoftResetOptions&) {}

void TTSimTTDevice::send_tensix_risc_reset(const TensixSoftResetOptions&) {}

void TTSimTTDevice::assert_risc_reset(tt_xy_pair, const RiscType) {}

void TTSimTTDevice::deassert_risc_reset(tt_xy_pair, const RiscType, bool) {}

void TTSimTTDevice::retrain_dram_core(const uint32_t) {}

// ---------------------------------------------------------------------------
// RtlSimulationTTDevice
// ---------------------------------------------------------------------------

RtlSimulationTTDevice::RtlSimulationTTDevice(const std::filesystem::path&, SocDescriptor, ChipId, int) {
    unsupported("RtlSimulationTTDevice");
}

RtlSimulationTTDevice::~RtlSimulationTTDevice() = default;

TLBManager* RtlSimulationTTDevice::get_tlb_manager() { return nullptr; }

void RtlSimulationTTDevice::read_from_device(void*, tt_xy_pair, uint64_t, uint32_t) {}

void RtlSimulationTTDevice::write_to_device(const void*, tt_xy_pair, uint64_t, uint32_t) {}

void RtlSimulationTTDevice::dma_d2h(void*, uint32_t, size_t) {}

void RtlSimulationTTDevice::dma_d2h_zero_copy(void*, uint32_t, size_t) {}

void RtlSimulationTTDevice::dma_h2d(uint32_t, const void*, size_t) {}

void RtlSimulationTTDevice::dma_h2d_zero_copy(uint32_t, const void*, size_t) {}

void RtlSimulationTTDevice::read_from_arc_apb(void*, uint64_t, size_t) {}

void RtlSimulationTTDevice::write_to_arc_apb(const void*, uint64_t, size_t) {}

void RtlSimulationTTDevice::read_from_arc_csm(void*, uint64_t, size_t) {}

void RtlSimulationTTDevice::write_to_arc_csm(const void*, uint64_t, size_t) {}

void RtlSimulationTTDevice::wait_arc_core_start(std::chrono::milliseconds) {}

std::chrono::milliseconds RtlSimulationTTDevice::wait_eth_core_training(tt_xy_pair, std::chrono::milliseconds) {
    return std::chrono::milliseconds{0};
}

EthTrainingStatus RtlSimulationTTDevice::read_eth_core_training_status(tt_xy_pair) {
    return EthTrainingStatus::NOT_CONNECTED;
}

uint32_t RtlSimulationTTDevice::get_clock() { return 0; }

uint32_t RtlSimulationTTDevice::get_min_clock_freq() { return 0; }

bool RtlSimulationTTDevice::get_noc_translation_enabled() { return false; }

void RtlSimulationTTDevice::dma_multicast_write(void*, size_t, tt_xy_pair, tt_xy_pair, uint64_t) {}

void RtlSimulationTTDevice::send_tensix_risc_reset(tt_xy_pair, const TensixSoftResetOptions&) {}

void RtlSimulationTTDevice::send_tensix_risc_reset(const TensixSoftResetOptions&) {}

void RtlSimulationTTDevice::assert_risc_reset(tt_xy_pair, const RiscType) {}

void RtlSimulationTTDevice::deassert_risc_reset(tt_xy_pair, const RiscType, bool) {}

void RtlSimulationTTDevice::retrain_dram_core(const uint32_t) {}

}  // namespace tt::umd
