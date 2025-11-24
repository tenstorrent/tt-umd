/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>

namespace nb = nanobind;

// Forward declarations for binding functions from each module.
void bind_basic_types(nb::module_ &m);
void bind_cluster(nb::module_ &m);
void bind_tt_device(nb::module_ &m);
void bind_telemetry(nb::module_ &m);
void bind_topology_discovery(nb::module_ &m);
void bind_warm_reset(nb::module_ &m);
void bind_soc_descriptor(nb::module_ &m);

// Main module entry point.
NB_MODULE(tt_umd, m) {
    bind_basic_types(m);
    bind_cluster(m);
    bind_tt_device(m);
    bind_telemetry(m);
    bind_topology_discovery(m);
    bind_warm_reset(m);
    bind_soc_descriptor(m);
}
