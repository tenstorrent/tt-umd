// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Deprecated header. Include "umd/device/chip_helpers/system_memory_buffer.hpp" instead.
//
// This path and the SysmemBuffer name are kept so downstream repos that vendor UMD can uplift before
// migrating. Both go away in the follow-up that removes the deprecated forwarders.
#include "umd/device/chip_helpers/system_memory_buffer.hpp"

namespace tt::umd {

using SysmemBuffer [[deprecated("Use SystemMemoryBuffer instead.")]] = SystemMemoryBuffer;

}  // namespace tt::umd
