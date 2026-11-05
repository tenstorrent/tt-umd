// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/tt_device/protocol/device_protocol.hpp"

namespace tt::umd {

class SimulationTTDevice;

/**
 * @brief A simulation device's own NOC path served as a DeviceProtocol, so components written against
 * the protocol (a RiscReset, say) work on a simulation device as they do on silicon. Every access is
 * synchronous and ordered, so the ctrl variants are the data ones, one register wide; no multicast is
 * modelled.
 *
 * Non-owning: the device must outlive this object.
 */
class SimulationNocProtocol : public DeviceProtocol {
public:
    SimulationNocProtocol(SimulationTTDevice* device, int mmio_id);

    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(
        void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    void write_ctrl(
        const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;

    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;

    int get_mmio_id() override;

private:
    SimulationTTDevice* device_;
    int mmio_id_;
};

}  // namespace tt::umd
