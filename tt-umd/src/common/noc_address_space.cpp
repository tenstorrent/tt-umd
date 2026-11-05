// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "noc_address_space.hpp"

#include <fmt/format.h>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

NocAddressSpace noc_address_space(const NocId noc_id) {
    switch (noc_id) {
        case NocId::NOC0:
            return NocAddressSpace::SystemPhysical;
        case NocId::SYSTEM_NOC:
            return NocAddressSpace::DeviceLocal;
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "The driver reaches a target through one inbound aperture per address space, and {} is not one of "
                    "them.",
                    noc_to_str(noc_id)));
    }
}

}  // namespace tt::umd
