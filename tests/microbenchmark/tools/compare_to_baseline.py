# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Local wrapper around `summarize_regressions.py`.

Run benchmarks locally (with `UMD_MICROBENCHMARK_RESULTS_PATH` set so the C++
exporter actually writes JSON), then point this script at the resulting
directory and your arch to get a markdown regression report against the
in-repo `baselines.yaml`.

Example:

    export UMD_MICROBENCHMARK_RESULTS_PATH=/tmp/umd-bench
    mkdir -p "$UMD_MICROBENCHMARK_RESULTS_PATH"
    ./build/tests/microbenchmark/microbenchmark_tests \\
        --gtest_filter='MicrobenchmarkTLB.CompareMulticastandUnicast'

    python3 tests/microbenchmark/tools/compare_to_baseline.py --arch 'WH n150'

The summary is printed to stdout. Exit code is 1 if any `gate: true` case in
`baselines.yaml` breached as DOWN/CRIT (same gating rule as the CI workflow);
0 otherwise.
"""

import argparse
import os
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
from summarize_regressions import (  # noqa: E402
    ARCH_PATTERNS,
    read_arch_results,
    render_summary,
)

KNOWN_ARCH_LABELS = [label for label, _ in ARCH_PATTERNS]
BASELINES_DEFAULT = Path(__file__).resolve().parents[1] / "expected" / "baselines.yaml"


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
        choices=KNOWN_ARCH_LABELS,
        help="Arch label to compare against (must match a key in baselines.yaml).",
    )
    p.add_argument(
        "--baselines",
        type=Path,
        default=BASELINES_DEFAULT,
        help=f"Path to baselines.yaml (default: {BASELINES_DEFAULT}).",
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
    if not args.baselines.is_file():
        sys.exit(f"--baselines is not a file: {args.baselines}")

    with open(args.baselines) as f:
        baselines = yaml.safe_load(f) or {}

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
