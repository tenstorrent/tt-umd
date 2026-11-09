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
#include <string_view>
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
        return fmt::format("{:g} {}", value, unit);
    }
    if (value >= ONE_GIB) {
        return fmt::format("{:g} GiB", value / ONE_GIB);
    }
    if (value >= ONE_MIB) {
        return fmt::format("{:g} MiB", value / ONE_MIB);
    }
    if (value >= ONE_KIB) {
        return fmt::format("{:g} KiB", value / ONE_KIB);
    }
    return fmt::format("{:g} B", value);
}

inline constexpr std::string_view CONTEXT_PLACEHOLDER = "@CONTEXT@";

// Variant of nanobench's stock JSON template with flat field names instead of parenthesised keys such as
// "median(elapsed)", which are awkward database field names. The raw measurements are left out and the per-result
// context is added. All values are nanobench's own, derived values are left to the consumer of the JSON.
inline constexpr std::string_view JSON_TEMPLATE = R"DELIM({
    "results": [
{{#result}}        {
            "context": @CONTEXT@,
            "title": "{{title}}",
            "name": "{{name}}",
            "unit": "{{unit}}",
            "batch": {{batch}},
            "elapsed": {{median(elapsed)}},
            "elapsed_error": {{medianAbsolutePercentError(elapsed)}}
        }{{^-last}},{{/-last}}
{{/result}}    ]
})DELIM";

// Fills the template's "context" object with the given context variables. Every rendered result must have set each
// listed variable via Bench::context(), otherwise rendering throws.
inline std::string make_json_template(const std::vector<std::string>& context_variables) {
    std::string context = "{";
    for (const std::string& variable : context_variables) {
        const char* separator = context.size() > 1 ? ", " : "";
        context += fmt::format(R"({}"{}": "{{{{context({})}}}}")", separator, variable, variable);
    }
    context += "}";
    std::string tmpl(JSON_TEMPLATE);
    tmpl.replace(tmpl.find(CONTEXT_PLACEHOLDER), CONTEXT_PLACEHOLDER.size(), context);
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
