// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "arc/blackhole_arc_telemetry_reader.hpp"

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"

namespace tt::umd {

namespace {

// ARC CSM, where the firmware keeps the telemetry table and data. A pointer outside it is not a
// published one.
constexpr uint64_t ARC_CSM_BASE = 0x10000000;
constexpr uint64_t ARC_CSM_SIZE = 512 * 1024;

bool is_inside_csm(uint32_t arc_addr) { return arc_addr >= ARC_CSM_BASE && arc_addr < ARC_CSM_BASE + ARC_CSM_SIZE; }

}  // namespace

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1) {}

void BlackholeArcTelemetryReader::get_telemetry_address() {
    const NocId noc_id = get_selected_noc_id();
    const tt_xy_pair arc_core = get_arc_core(noc_id);

    // SCRATCH_RAM[12] (data) and [13] (table) are adjacent; ARC addresses are NOC addresses on the
    // ARC tile, and the CSM pointers they hold are used as they are.
    uint32_t pointers[2] = {0, 0};
    device_protocol->read_ctrl(
        pointers,
        arc_core,
        blackhole::ARC_NOC_XBAR_ADDRESS_START + blackhole::SCRATCH_RAM_12,
        sizeof(pointers),
        noc_id,
        RegisterAccessWidth::BITS32);
    const uint32_t data_addr = pointers[0];
    const uint32_t table_addr = pointers[1];

    // The firmware writes the data pointer first and the table pointer last, so a valid table
    // pointer implies a valid data pointer; anything outside CSM counts as not yet published.
    const bool published = is_inside_csm(table_addr) && is_inside_csm(data_addr);
    telemetry_table_addr_reg = published ? table_addr : 0;
    telemetry_table_addr = telemetry_table_addr_reg;
    telemetry_values_addr = published ? data_addr : 0;
}

}  // namespace tt::umd
