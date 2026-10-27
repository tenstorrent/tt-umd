// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/types/noc_id.hpp"

namespace tt::umd {

/**
 * The address space @p noc_id selects, so that everything issuing scalar accesses agrees on which
 * aperture a NOC means.
 *
 * @throws error::RuntimeError for a NOC no inbound aperture serves.
 */
NocAddressSpace noc_address_space(NocId noc_id);

}  // namespace tt::umd
