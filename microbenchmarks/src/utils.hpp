// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <fmt/format.h>
#include <nanobench.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <tt-umd/types/cluster_descriptor_types.hpp>
#include <vector>

using namespace ankerl::nanobench;

namespace tt::umd::microbenchmarks {

inline const char* OUTPUT_ENV_VAR = "UMD_MICROBENCHMARK_RESULTS_PATH";

inline constexpr size_t ONE_KIB = 1 << 10;
inline constexpr size_t ONE_MIB = 1 << 20;
inline constexpr size_t ONE_GIB = 1 << 30;

// Formats a value as "<value> <unit>". An empty or "byte" unit is scaled to B/KiB/MiB/GiB, any other unit is printed
// as is.
inline std::string format_with_unit(double value, const std::string& unit) {
    if (!unit.empty() && unit != "byte") {
        return fmt::format("{:.2f} {}", value, unit);
    }
    if (value >= ONE_GIB) {
        return fmt::format("{:.2f} GiB", value / ONE_GIB);
    }
    if (value >= ONE_MIB) {
        return fmt::format("{:.2f} MiB", value / ONE_MIB);
    }
    if (value >= ONE_KIB) {
        return fmt::format("{:.2f} KiB", value / ONE_KIB);
    }
    return fmt::format("{:.2f} B", value);
}

// Builds nanobench's JSON template with a "context" object per result holding the given context variables, since the
// stock template omits them. Every rendered result must have set each listed variable via Bench::context(), otherwise
// rendering throws.
inline std::string make_json_template(const std::vector<std::string>& context_variables) {
    std::string tmpl = ankerl::nanobench::templates::json();
    if (context_variables.empty()) {
        return tmpl;
    }
    std::string context = "\"context\": {";
    for (const std::string& variable : context_variables) {
        context += fmt::format(R"("{0}": "{{{{context({0})}}}}",)", variable);
    }
    context.back() = '}';  // Replaces the last variable's trailing comma.
    tmpl.insert(tmpl.find("\"title\""), context + ",\n            ");
    return tmpl;
}

inline void export_results(
    const std::string& title,
    std::vector<Result> const& results,
    const std::vector<std::string>& context_variables = {}) {
    const char* results_path = std::getenv(OUTPUT_ENV_VAR);
    if (results_path == nullptr) {
        std::cout << OUTPUT_ENV_VAR << " not set. Results will not exported." << std::endl;
        return;
    }
    std::filesystem::path filepath = std::filesystem::path(results_path) / (title + ".json");
    std::ofstream file(filepath);
    ankerl::nanobench::render(make_json_template(context_variables).c_str(), results, file);
}

inline void export_results(const Bench& bench, const std::vector<std::string>& context_variables = {}) {
    export_results(bench.title(), bench.results(), context_variables);
}

}  // namespace tt::umd::microbenchmarks
