# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Local wrapper around `summarize_regressions.py`.

Run benchmarks locally (with `UMD_MICROBENCHMARK_RESULTS_PATH` set so the C++
exporter actually writes JSON), then point this script at the resulting
directory and your arch to get a markdown regression report against the
in-repo per-arch baseline YAML for that arch. Intended to be run locally on some of our dedicated runners.

Example:

    export UMD_MICROBENCHMARK_RESULTS_PATH=/tmp/umd-bench
    mkdir -p "$UMD_MICROBENCHMARK_RESULTS_PATH"
    ./build/test/umd/microbenchmark/umd_microbenchmark \\
        --gtest_filter='MicrobenchmarkOpenCluster.ClusterConstructor'

    python3 tests/microbenchmark/tools/compare_to_baseline.py --arch 'n150'

See `summarize_regressions.py` for the input/baseline formats and the
comparison rule; this is just a single-arch local wrapper around it.

The summary is printed to stdout. Exit code is 1 if any `gate: true` case in
the per-arch baseline YAML breached as DOWN (same gating rule as the CI
workflow); 0 otherwise.
"""

import argparse
import os
import sys
from pathlib import Path

from summarize_regressions import (
    load_baselines_dir,
    read_arch_results,
    render_summary,
)

BASELINES_DIR_DEFAULT = Path(__file__).resolve().parents[1] / "baselines"


def _filter_baselines_to_arch(combined: dict, arch: str) -> dict:
    """Return a copy of the combined baselines dict containing only `arch`'s
    column under each (title, case). The shape is the same as the input
    (`{ test: { case: { arch: entry } } }` + `metadata.archs`), so it can be
    fed straight into `render_summary` for a single-arch local comparison.
    """
    archs_meta = (combined.get("metadata") or {}).get("archs") or {}
    filtered: dict = {"metadata": {"archs": {}}}
    if arch in archs_meta:
        filtered["metadata"]["archs"][arch] = archs_meta[arch]
    for title, cases in combined.items():
        if title == "metadata":
            continue
        for case_name, arch_entries in cases.items():
            if arch in arch_entries:
                filtered.setdefault(title, {}).setdefault(case_name, {})[arch] = (
                    arch_entries[arch]
                )
    return filtered


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--results",
        type=Path,
        default=os.environ.get("UMD_MICROBENCHMARK_RESULTS_PATH"),
        help=(
            "Directory holding local <title>.json files. "
            "Defaults to $UMD_MICROBENCHMARK_RESULTS_PATH."
        ),
    )
    p.add_argument(
        "--arch",
        required=True,
        type=str,
        help="Arch label to compare against (must match a file in --baselines-dir).",
    )
    p.add_argument(
        "--baselines-dir",
        type=Path,
        default=BASELINES_DIR_DEFAULT,
        help=(
            "Directory of per-arch baseline YAMLs "
            f"(default: {BASELINES_DIR_DEFAULT})."
        ),
    )
    p.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Optional path to also write the markdown summary to.",
    )
    args = p.parse_args()

    if args.results is None:
        sys.exit(
            "No results path. Either pass --results or set "
            "UMD_MICROBENCHMARK_RESULTS_PATH before running the benchmarks."
        )
    if not args.results.is_dir():
        sys.exit(f"--results is not a directory: {args.results}")
    if not args.baselines_dir.is_dir():
        sys.exit(f"--baselines-dir is not a directory: {args.baselines_dir}")

    combined = load_baselines_dir(args.baselines_dir)
    if args.arch not in (combined.get("metadata") or {}).get("archs", {}):
        sys.exit(
            f"No baseline YAML for arch '{args.arch}' found in {args.baselines_dir}. "
            f"Known archs: {sorted((combined.get('metadata') or {}).get('archs', {}))}"
        )
    baselines = _filter_baselines_to_arch(combined, args.arch)

    per_arch = read_arch_results(args.results, args.arch)
    if not per_arch:
        sys.exit(
            f"No usable JSON results found under {args.results}. Did the run "
            f"produce <title>.json files? (Check UMD_MICROBENCHMARK_RESULTS_PATH.)"
        )

    # render_summary expects { arch_label: { test_title: { case_name: ... } } }.
    # For local mode we only have one arch — the table renders with a single column.
    current = {args.arch: per_arch}
    markdown, gated_breaches, missing_gated = render_summary(current, baselines)

    print(markdown)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(markdown)

    if missing_gated:
        print(
            f"\nWARN: {len(missing_gated)} gated case(s) missing from this run "
            f"on {args.arch}.",
            file=sys.stderr,
        )
        for title, case, arch in missing_gated:
            print(f"  - {title} :: {case} :: {arch}", file=sys.stderr)
    if gated_breaches:
        print(
            f"\nFAIL: {len(gated_breaches)} gated case(s) breached tolerance "
            f"on {args.arch}.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
