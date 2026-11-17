// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/ip_block_layout.hpp"

#include "common/unsupported.hpp"

namespace tt::umd {

void validate_ip_block_layout(const IpBlockLayout& layout) { UMD_NOT_SUPPORTED("IP block layouts (simulation)"); }

}  // namespace tt::umd
