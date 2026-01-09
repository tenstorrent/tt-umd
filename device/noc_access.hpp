// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "api/umd/device/types/noc_id.hpp"

namespace tt::umd {

NocId get_selected_noc_id();

bool is_selected_noc1();

}  // namespace tt::umd
