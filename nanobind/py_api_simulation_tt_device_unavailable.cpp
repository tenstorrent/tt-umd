// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>

namespace nb = nanobind;

// Built without simulation support, where no simulation TTDevice can exist, so there is no subclass to bind.
void bind_simulation_tt_devices(nb::module_ &) {}
