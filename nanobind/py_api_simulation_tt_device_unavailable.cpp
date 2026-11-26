// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>

namespace nb = nanobind;

// Built without simulation support, or from install artifacts, which do not ship the simulation TTDevice headers, so
// there is no subclass to bind.
void bind_simulation_tt_devices(nb::module_ &) {}
