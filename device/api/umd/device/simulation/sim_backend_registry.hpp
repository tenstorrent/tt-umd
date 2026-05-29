// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>

#include "umd/device/simulation/sim_backend.hpp"

namespace tt::umd {

/**
 * Select and construct the ISimBackend best suited to a given simulator shared library.
 *
 * Selection is by full-set probing: each registered backend declares a MINIMUM required
 * symbol set. We dlopen the candidate library once, and for each backend check whether its
 * entire minimum set resolves. The winner is the backend whose required set fully resolves,
 * tie-broken by the most symbols matched (so a richer/more-specific backend wins over a
 * generic fallback that happens to share a subset).
 *
 * Today only LibTTSimBackend matches; a second backend stub documents how a divergent
 * simulator ABI would slot in.
 */
std::unique_ptr<ISimBackend> create_sim_backend(
    const std::filesystem::path &simulator_path, bool copy_sim_binary = false);

}  // namespace tt::umd
