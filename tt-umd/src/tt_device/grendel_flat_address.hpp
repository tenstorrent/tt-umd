// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// INTERNAL, chippy-aware header (C++20). Do not include from public UMD headers.

#include <memory>
#include <string>
#include <vector>

#include "transport_interface.h"  // chippy

#include "flat_address_transport.hpp"

namespace tt::umd {

class SocDescriptor;

/**
 * Flat-address transport for the descriptor's address space.
 *
 * mimir_local folds each flat window back to a chiplet-local AXI address on that Mimir's
 * transport. full_package keeps one transport and puts the flat address on the wire as the SPA.
 */
std::unique_ptr<FlatAddressTransport> make_grendel_flat_address_transport(
    const SocDescriptor& soc_descriptor,
    std::vector<std::shared_ptr<chippy::transport::TransportInterface>> transports,
    std::string device_name);

}  // namespace tt::umd
