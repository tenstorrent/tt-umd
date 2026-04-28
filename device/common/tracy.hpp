// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// When Tracy is disabled, we define no-op stubs for the macros we use.
// Expand this list as more Tracy macros are adopted.
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScoped
#define ZoneScopedN(name)
#define ZoneScopedNC(name, color)
#define ZoneScopedC(color)
#define TracyAlloc(ptr, size)
#define TracyFree(ptr)
#define TracyAllocN(ptr, size, name)
#define TracyFreeN(ptr, name)
#endif
