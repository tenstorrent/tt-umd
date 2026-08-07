// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_device_identity.hpp"

#include <fmt/format.h>
#include <unistd.h>  // write, close (temp-file materialization via mkstemps)

#include <cerrno>
#include <cstdlib>  // mkstemps
#include <cstring>  // strerror, strlen
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

// A temp file that removes itself on destruction (best-effort), so callers don't leak it on a
// throw between creation and cleanup.
struct ScopedTempFile {
    std::filesystem::path path;

    ~ScopedTempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// Materialize `contents` to a unique temp file ending in `suffix` (mkstemps avoids name races).
ScopedTempFile write_temp_file(const std::string& contents, const char* suffix) {
    const std::string name_template =
        (std::filesystem::temp_directory_path() / (std::string("tt-umd-sim-socdesc-XXXXXX") + suffix)).string();
    std::vector<char> path_buf(name_template.begin(), name_template.end());
    path_buf.push_back('\0');
    const int fd = ::mkstemps(path_buf.data(), static_cast<int>(std::strlen(suffix)));
    UMD_ASSERT(
        fd >= 0,
        error::RuntimeError,
        fmt::format("Failed to create a temp SoC descriptor file: {}", std::strerror(errno)));
    const ssize_t written = ::write(fd, contents.data(), contents.size());
    ::close(fd);
    UMD_ASSERT(
        written == static_cast<ssize_t>(contents.size()),
        error::RuntimeError,
        fmt::format("Failed to write temp file {}", path_buf.data()));
    return ScopedTempFile{std::filesystem::path(path_buf.data())};
}

}  // namespace

SimulationServerDeviceInfo describe_device(const SocDescriptor& soc_descriptor, SimulationBackendType backend_type) {
    const std::string& yaml_path = soc_descriptor.get_arch_descriptor().get_device_descriptor_file_path();
    UMD_ASSERT(
        !yaml_path.empty(),
        error::RuntimeError,
        "Cannot serve device info: the SoC descriptor was not built from a YAML file.");

    std::ifstream in(yaml_path);
    UMD_ASSERT(in.good(), error::RuntimeError, fmt::format("Failed to open SoC descriptor YAML at {}", yaml_path));
    std::stringstream buffer;
    buffer << in.rdbuf();

    SimulationServerDeviceInfo info;
    info.status = 0;
    info.arch = static_cast<int32_t>(soc_descriptor.arch);
    info.backend_type = backend_type;
    info.soc_descriptor_yaml = buffer.str();
    info.noc_translation_enabled = soc_descriptor.noc_translation_enabled;
    info.tensix_harvesting_mask = soc_descriptor.harvesting_masks.tensix_harvesting_mask;
    info.dram_harvesting_mask = soc_descriptor.harvesting_masks.dram_harvesting_mask;
    info.eth_harvesting_mask = soc_descriptor.harvesting_masks.eth_harvesting_mask;
    info.l2cpu_harvesting_mask = soc_descriptor.harvesting_masks.l2cpu_harvesting_mask;
    info.pcie_harvesting_mask = soc_descriptor.harvesting_masks.pcie_harvesting_mask;
    return info;
}

SocDescriptor build_soc_descriptor(const SimulationServerDeviceInfo& device_info) {
    // SocArchDescriptor constructs only from a path, so materialize the served YAML to a unique temp
    // file, build from it, and let RAII remove it -- including if construction or the arch check
    // below throws. The descriptor reads the YAML eagerly, so the file isn't needed afterwards.
    const ScopedTempFile yaml_file = write_temp_file(device_info.soc_descriptor_yaml, ".yaml");

    ChipInfo chip_info{};
    chip_info.noc_translation_enabled = device_info.noc_translation_enabled;
    chip_info.harvesting_masks.tensix_harvesting_mask = device_info.tensix_harvesting_mask;
    chip_info.harvesting_masks.dram_harvesting_mask = device_info.dram_harvesting_mask;
    chip_info.harvesting_masks.eth_harvesting_mask = device_info.eth_harvesting_mask;
    chip_info.harvesting_masks.l2cpu_harvesting_mask = device_info.l2cpu_harvesting_mask;
    chip_info.harvesting_masks.pcie_harvesting_mask = device_info.pcie_harvesting_mask;

    SocDescriptor soc_descriptor(std::make_shared<SocArchDescriptor>(yaml_file.path.string()), chip_info);

    // The arch is transported explicitly (not just implied by the YAML) so a mismatch between the
    // served YAML and the served arch value is caught here rather than silently producing a
    // descriptor that disagrees with what the host reported.
    UMD_ASSERT(
        static_cast<int32_t>(soc_descriptor.arch) == device_info.arch,
        error::RuntimeError,
        fmt::format(
            "Device-info arch ({}) does not match SoC-descriptor YAML arch ({})",
            device_info.arch,
            static_cast<int32_t>(soc_descriptor.arch)));

    return soc_descriptor;
}

SimulationServerDeviceInfo fetch_device_info_from_host(SimulationClient& client) {
    client.attach();  // idempotent; safe if already attached
    SimulationServerRequest request;
    request.command = SimulationServerCommand::GetDeviceInfo;
    const SimulationServerDeviceInfo info = decode_device_info(client.transact(encode(request)));
    UMD_ASSERT(
        info.status == 0,
        error::RuntimeError,
        fmt::format("Remote simulation host failed to serve device info (status {})", info.status));
    return info;
}

SimulationServerClusterDescriptor describe_cluster(const std::filesystem::path& simulator_directory) {
    SimulationServerClusterDescriptor cluster_descriptor;
    cluster_descriptor.status = 0;

    const std::string yaml_path = SimulationChip::get_cluster_descriptor_path_from_simulator_path(simulator_directory);
    // A simulator build need not ship a cluster_descriptor.yaml; an empty yaml tells the client to
    // fall back to a mock built from the device info, mirroring the host's own mock fallback.
    // Distinguish "definitely absent" from "couldn't tell": if exists() itself errors (permission/
    // IO), surface it rather than silently serving an empty descriptor as if the file were absent.
    std::error_code ec;
    const bool yaml_exists = std::filesystem::exists(yaml_path, ec);
    UMD_ASSERT(
        !ec,
        error::RuntimeError,
        fmt::format("Failed to check for cluster descriptor at {}: {}", yaml_path, ec.message()));
    if (!yaml_exists) {
        return cluster_descriptor;
    }

    std::ifstream in(yaml_path);
    UMD_ASSERT(in.good(), error::RuntimeError, fmt::format("Failed to open cluster descriptor YAML at {}", yaml_path));
    std::stringstream buffer;
    buffer << in.rdbuf();
    cluster_descriptor.yaml = buffer.str();
    return cluster_descriptor;
}

std::string fetch_cluster_descriptor_yaml(SimulationClient& client) {
    client.attach();  // idempotent; safe if already attached
    SimulationServerRequest request;
    request.command = SimulationServerCommand::GetClusterDescriptor;
    const SimulationServerClusterDescriptor cluster_descriptor =
        decode_cluster_descriptor(client.transact(encode(request)));
    UMD_ASSERT(
        cluster_descriptor.status == 0,
        error::RuntimeError,
        fmt::format(
            "Remote simulation host failed to serve cluster descriptor (status {})", cluster_descriptor.status));
    return cluster_descriptor.yaml;
}

}  // namespace tt::umd
