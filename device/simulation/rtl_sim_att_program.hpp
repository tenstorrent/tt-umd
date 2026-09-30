// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

#include "umd/device/topology/ip_layout.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class RtlSimCommunicator;

/**
 * ATT programming of an RTL simulation/emulation build, read from
 * <simulator_directory>/att_program.yaml: per device, the register writes that set up its ATTs
 * before any core runs, in order. The build's own tooling generates the file from its ATT map; UMD
 * only replays it, so it holds no product-specific ATT logic.
 */
class RtlSimAttProgram {
public:
    static constexpr const char* FILE_NAME = "att_program.yaml";

    /**
     * One register write of the program: `value` written to `address` on tile `core`. A list of
     * writes rather than of registers, because order matters and a register can appear twice: each
     * ATT is disabled first and enabled last.
     */
    struct RegisterWrite {
        tt_xy_pair core;
        uint64_t address;
        uint32_t value;
    };

    /** The build's ATT program, or nullopt if it has none. */
    static std::optional<RtlSimAttProgram> load(const std::filesystem::path& simulator_directory);

    explicit RtlSimAttProgram(const std::filesystem::path& file);

    /** @p device's writes, in order; empty if the program doesn't list it. */
    const std::vector<RegisterWrite>& get_writes(IpDeviceId device) const;

    /** Replay @p device's writes through @p communicator, reading each back; throws on a mismatch. */
    void apply(IpDeviceId device, RtlSimCommunicator& communicator) const;

private:
    std::map<IpDeviceId, std::vector<RegisterWrite>> writes_;
};

}  // namespace tt::umd
