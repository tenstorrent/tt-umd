// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/architecture_tlbs.hpp"

#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

tlb_configuration ArchitectureTlbs::get_configuration(uint32_t tlb_index) const { UMD_NOT_IMPLEMENTED(); }

tlb_static_vc ArchitectureTlbs::get_static_vc(WindowFlags flags) const { UMD_NOT_IMPLEMENTED(); }

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
