// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/sim_backend_registry.hpp"

#include <dlfcn.h>
#include <fmt/format.h>

#include <functional>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/simulation/libttsim_backend.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

// A registered backend candidate: how to probe it, how to build it.
struct BackendCandidate {
    const char *name;
    // Minimum symbol set that must fully resolve for this backend to be a candidate.
    std::function<const std::vector<const char *> &()> minimum_symbols;
    // Factory used once this candidate is selected.
    std::function<std::unique_ptr<ISimBackend>(const std::filesystem::path &, bool)> make;
};

const std::vector<BackendCandidate> &registry() {
    static const std::vector<BackendCandidate> kCandidates = {
        {
            "LibTTSimBackend",
            &LibTTSimBackend::minimum_required_symbols,
            [](const std::filesystem::path &p, bool copy) -> std::unique_ptr<ISimBackend> {
                return std::make_unique<LibTTSimBackend>(p, copy);
            },
        },
        // TODO(part-a): second backend stub. When a divergent simulator ABI lands (renamed /
        // added symbols totally out of sync with today's libttsim.so), register it here, e.g.:
        //
        //   {
        //       "CraqSimBackend",
        //       &CraqSimBackend::minimum_required_symbols,   // e.g. {"craqsim_init", "craqsim_step", ...}
        //       [](auto p, auto copy) { return std::make_unique<CraqSimBackend>(p, copy); },
        //   },
        //
        // Selection below already handles "most symbols matched" tie-breaking, so a richer
        // backend whose fuller set resolves wins over LibTTSimBackend automatically.
    };
    return kCandidates;
}

// Count how many of `names` resolve in an already-open handle.
size_t count_resolved(void *handle, const std::vector<const char *> &names) {
    size_t n = 0;
    for (const char *name : names) {
        if (dlsym(handle, name)) {
            ++n;
        }
    }
    return n;
}

}  // namespace

std::unique_ptr<ISimBackend> create_sim_backend(
    const std::filesystem::path &simulator_path, bool copy_sim_binary) {
    // Probe against the on-disk file once. We deliberately dlopen here (not the memfd copy):
    // probing is read-only symbol resolution, and the selected backend re-opens (optionally
    // from a sealed memfd) when constructed. RTLD_LOCAL keeps the probe handle isolated.
    void *probe = dlopen(simulator_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!probe) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to dlopen for backend probe: {}", dlerror()));
    }

    const BackendCandidate *winner = nullptr;
    size_t winner_score = 0;
    for (const auto &cand : registry()) {
        const std::vector<const char *> &required = cand.minimum_symbols();
        size_t matched = count_resolved(probe, required);
        bool full = (matched == required.size());
        log_debug(
            tt::LogEmulationDriver,
            "Backend probe: {} matched {}/{} required symbols (full={})",
            cand.name,
            matched,
            required.size(),
            full);
        // Candidate is only eligible if its ENTIRE minimum set resolves. Tie-break: most matched.
        if (full && matched > winner_score) {
            winner = &cand;
            winner_score = matched;
        }
    }

    dlclose(probe);

    if (!winner) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("No registered sim backend's required symbol set resolves in {}", simulator_path.string()));
    }
    log_info(tt::LogEmulationDriver, "Selected sim backend: {}", winner->name);
    return winner->make(simulator_path, copy_sim_binary);
}

}  // namespace tt::umd
