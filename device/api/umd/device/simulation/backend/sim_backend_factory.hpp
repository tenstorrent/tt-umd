// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>

#include "umd/device/simulation/backend/sim_backend.hpp"

namespace tt::umd {

/**
 * Selects and constructs an ISimBackend for a simulator .so.
 *
 * Selection is driven by the simulator's *self-declared* identity:
 *   1. load the .so and read libttsim_variant() + libttsim_abi_version(),
 *   2. assert the ABI version is one UMD understands (refuse otherwise),
 *   3. dispatch on the variant string to the matching backend implementation.
 *
 * There is no dispatch on file extension and no sibling soc_descriptor.yaml.
 */
class SimBackendFactory {
public:
    // Loads `so_path`, performs the identity/ABI/capability handshake, and returns
    // a backend bound to a freshly created per-device handle (`device_id`).
    static std::unique_ptr<ISimBackend> create(const std::filesystem::path& so_path, uint32_t device_id = 0);
};

}  // namespace tt::umd
