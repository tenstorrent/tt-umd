// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>

#include "umd/device/logging/config.hpp"

namespace nb = nanobind;

using namespace tt::umd::logging;

void bind_logging(nb::module_ &m) {
    auto logging_module = m.def_submodule("logging", "UMD logging configuration");

    nb::enum_<level>(logging_module, "Level")
        .value("TRACE", level::trace, "Most detailed logging level, for tracing program execution")
        .value("DEBUG", level::debug, "Debugging information, useful during development")
        .value("INFO", level::info, "General informational messages about program operation")
        .value("WARN", level::warn, "Warning messages for potentially harmful situations")
        .value("ERROR", level::error, "Error messages for serious problems")
        .value("CRITICAL", level::critical, "Critical errors that may lead to program termination")
        .value("OFF", level::off, "Disables all logging");

    logging_module.def(
        "set_level",
        &set_level,
        nb::arg("lvl"),
        "Sets the global logging level. Messages with severity levels lower than this level will not be logged.");
}
