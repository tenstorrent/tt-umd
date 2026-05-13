// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>

#include "umd/device/logging/config.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt::umd::logging;

void bind_logging(nb::module_ &m) {
    auto logging_module = m.def_submodule("logging", "UMD logging configuration");

    nb::enum_<level>(logging_module, "Level")
        .value("Trace", level::trace, "Most detailed logging level, for tracing program execution")
        .value("Debug", level::debug, "Debugging information, useful during development")
        .value("Info", level::info, "General informational messages about program operation")
        .value("Warning", level::warn, "Warning messages for potentially harmful situations")
        .value("Error", level::error, "Error messages for serious problems")
        .value("Critical", level::critical, "Critical errors that may lead to program termination")
        .value("Off", level::off, "Disables all logging");

    logging_module.def(
        "set_level",
        &set_level,
        nb::arg("lvl"),
        release_gil(),
        "Sets the global logging level. Messages with severity levels lower than this level will not be logged.");

    logging_module.def(
        "get_level",
        &get_level,
        release_gil(),
        "Returns the current global logging level. Pair with set_level() to save/restore the level "
        "around code that needs to temporarily change verbosity.");
}
