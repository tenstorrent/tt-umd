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

// A caller may supply the descriptor; otherwise it comes from the architecture's constants. A
// supplied descriptor is checked against the architecture it is being used for.
inline std::shared_ptr<SocArchDescriptor> resolve_soc_arch_descriptor(
    tt::ARCH arch, const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
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

// Architecture as a template parameter, so a model whose architecture is fixed at compile time
// states it once. A simulation model learns its architecture from the SoC descriptor it is built
// with and calls the overload above instead.
template <tt::ARCH arch>
std::shared_ptr<SocArchDescriptor> resolve_soc_arch_descriptor(
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    return resolve_soc_arch_descriptor(arch, soc_arch_descriptor);
}

}  // namespace tt::umd
