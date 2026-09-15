// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <mutex>

#include "umd/device/tt_device/reset/risc_reset.hpp"

namespace tt::umd {
class ArchitectureImplementation;

/**
 * @brief RiscReset over the TTSim backend: a read-modify-write of the soft-reset register through
 * the injected device I/O, with QSR's inverted DM-core reset polarity handled by a flag.
 *
 * The I/O is injected as callables (rather than a DeviceProtocol) because the simulation I/O path
 * still lives on the simulation TTDevice; swap them for the protocol once that moves into a
 * component. The injected dependencies are owned by the backend device and are not used during
 * destruction.
 */
class TTSimRiscReset : public RiscReset {
public:
    using ReadFn = std::function<void(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size)>;
    using WriteFn = std::function<void(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size)>;

    TTSimRiscReset(
        ReadFn read,
        WriteFn write,
        ArchitectureImplementation* architecture_impl,
        bool qsr_reset_polarity,
        std::recursive_mutex& device_lock);

    void assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id = NocId::DEFAULT_NOC) override;

    void deassert_risc_reset(
        tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id = NocId::DEFAULT_NOC) override;

    std::optional<RiscType> get_risc_reset_state(tt_xy_pair core, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    ReadFn read_;
    WriteFn write_;
    ArchitectureImplementation* architecture_impl_ = nullptr;
    bool qsr_reset_polarity_ = false;
    std::recursive_mutex& device_lock_;
};

}  // namespace tt::umd
