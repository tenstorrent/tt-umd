// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/rtl_sim_att_program.hpp"

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <tt-logger/tt-logger.hpp>

#include "umd/device/simulation/rtl_sim_communicator.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::optional<RtlSimAttProgram> RtlSimAttProgram::load(const std::filesystem::path& simulator_directory) {
    const std::filesystem::path path = simulator_directory / FILE_NAME;
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    return RtlSimAttProgram(path);
}

RtlSimAttProgram::RtlSimAttProgram(const std::filesystem::path& file) {
    const YAML::Node yaml = YAML::LoadFile(file.string());
    UMD_ASSERT(
        yaml["devices"].IsDefined() && yaml["devices"].IsSequence(),
        error::RuntimeError,
        fmt::format("{}: `devices` must be a list.", file.string()));
    for (const YAML::Node& node : yaml["devices"]) {
        const IpDeviceId device = static_cast<IpDeviceId>(node["id"].as<uint32_t>());
        UMD_ASSERT(
            node["writes"].IsDefined() && node["writes"].IsSequence(),
            error::RuntimeError,
            fmt::format("{}: device {} has no `writes` list.", file.string(), static_cast<uint32_t>(device)));
        std::vector<RegisterWrite> writes;
        for (const YAML::Node& write : node["writes"]) {
            UMD_ASSERT(
                write.size() == 4,
                error::RuntimeError,
                fmt::format("{}: a write is [x, y, address, value].", file.string()));
            writes.push_back(
                {tt_xy_pair(write[0].as<size_t>(), write[1].as<size_t>()),
                 write[2].as<uint64_t>(),
                 write[3].as<uint32_t>()});
        }
        UMD_ASSERT(
            writes_.emplace(device, std::move(writes)).second,
            error::RuntimeError,
            fmt::format("{} lists device {} twice.", file.string(), static_cast<uint32_t>(device)));
    }
}

const std::vector<RtlSimAttProgram::RegisterWrite>& RtlSimAttProgram::get_writes(IpDeviceId device) const {
    static const std::vector<RegisterWrite> none;
    const auto it = writes_.find(device);
    return it == writes_.end() ? none : it->second;
}

void RtlSimAttProgram::apply(IpDeviceId device, RtlSimCommunicator& communicator) const {
    const std::vector<RegisterWrite>& writes = get_writes(device);
    for (const RegisterWrite& write : writes) {
        communicator.tile_write_bytes(write.core.x, write.core.y, write.address, &write.value, sizeof(write.value));
        // Read each register back: a write to a wrong or unreachable ATT register is dropped silently
        // and would otherwise surface much later, as a hang. The read also completes the write before
        // the next one, so the program lands in order, and all of it before any core starts.
        uint32_t readback = 0;
        communicator.tile_read_bytes(write.core.x, write.core.y, write.address, &readback, sizeof(readback));
        UMD_ASSERT(
            readback == write.value,
            error::RuntimeError,
            fmt::format(
                "ATT program, device {}: ({}, {}) 0x{:x} reads back 0x{:x}, expected 0x{:x}.",
                static_cast<uint32_t>(device),
                write.core.x,
                write.core.y,
                write.address,
                readback,
                write.value));
    }
    log_info(LogUMD, "Device {}: ATT program applied, {} writes.", static_cast<uint32_t>(device), writes.size());
}

}  // namespace tt::umd
