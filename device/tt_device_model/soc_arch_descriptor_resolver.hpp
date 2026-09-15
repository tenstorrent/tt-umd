// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <memory>

#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// A caller may supply the descriptor; otherwise it comes from the architecture's constants. The
// architecture is a template parameter so each model states its own, and a supplied descriptor is
// checked against it.
template <tt::ARCH arch>
std::shared_ptr<SocArchDescriptor> resolve_soc_arch_descriptor(
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    if (soc_arch_descriptor == nullptr) {
        return std::make_shared<SocArchDescriptor>(arch);
    }
    UMD_ASSERT(
        soc_arch_descriptor->get_arch() == arch,
        error::RuntimeError,
        fmt::format(
            "SocArchDescriptor architecture {} does not match device architecture {}.",
            arch_to_str(soc_arch_descriptor->get_arch()),
            arch_to_str(arch)));
    return soc_arch_descriptor;
}

}  // namespace tt::umd
