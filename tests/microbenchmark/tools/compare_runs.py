# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Compare two timestamped microbenchmark runs in a results directory.

The C++ exporter writes each run into a subdirectory named after the run's
start time (e.g. `microbench-results/2026-05-08T14-22-31/ClusterConstructor.json`).
This script picks two such subdirectories — by default the two most recent —
and prints the relative throughput change per benchmark case using the same
diff logic as analyze_results.py.

Sign convention matches analyze_results.py: `Difference %` is
`(new - baseline) / baseline * 100`, so positive means the new run is faster.

Usage:
    # Default: most recent run vs. previous one
    python compare_runs.py microbench-results/

    # Pin either side explicitly
    python compare_runs.py microbench-results/ \\
        --new 2026-05-08T15-04-12 --baseline 2026-05-08T14-22-31
"""

import argparse
import sys
from pathlib import Path

# Reuse the per-benchmark diff logic from analyze_results.py.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_results import process_benchmark  # noqa: E402


def list_runs(root: Path) -> list[Path]:
    """Return run subdirectories that contain at least one JSON result, sorted oldest-first."""
    runs = [p for p in root.iterdir() if p.is_dir() and any(p.glob("*.json"))]
    # Timestamps in the filename format `%Y-%m-%dT%H-%M-%S` sort lexicographically by time.
    runs.sort(key=lambda p: p.name)
    return runs


def resolve_run(
    root: Path, name: str | None, fallback_index: int, runs: list[Path]
) -> Path:
    if name is None:
        if not runs:
            sys.exit(f"No timestamped runs found under {root}")
        if len(runs) + fallback_index < 0:
            sys.exit(
                f"Need at least {-fallback_index} run(s) under {root} to auto-resolve, found {len(runs)}"
            )
        return runs[fallback_index]
    candidate = root / name
    if not candidate.is_dir():
        sys.exit(f"Run not found: {candidate}")
    return candidate


def main():
    parser = argparse.ArgumentParser(description="Compare two UMD microbenchmark runs.")
    parser.add_argument(
        "results_dir",
        type=Path,
        help="Directory containing timestamped run subdirectories (UMD_MICROBENCHMARK_RESULTS_PATH).",
    )
    parser.add_argument(
        "--new",
        dest="new",
        help="Run directory name treated as the new measurement. Defaults to the most recent.",
    )
    parser.add_argument(
        "--baseline",
        dest="baseline",
        help="Run directory name treated as the baseline to compare against. "
        "Defaults to the second-most-recent.",
    )
    args = parser.parse_args()

    if not args.results_dir.is_dir():
        sys.exit(f"Not a directory: {args.results_dir}")

    runs = list_runs(args.results_dir)
    new_run = resolve_run(args.results_dir, args.new, -1, runs)
    baseline_run = resolve_run(args.results_dir, args.baseline, -2, runs)

    if new_run == baseline_run:
        sys.exit(f"--new and --baseline resolve to the same run: {new_run.name}")

    print(f"New:      {new_run.name}")
    print(f"Baseline: {baseline_run.name}")

    new_jsons = sorted(
        p for p in new_run.glob("*.json") if p.name != "machine_host_spec.json"
    )
    if not new_jsons:
        sys.exit(f"No benchmark JSONs in {new_run}")

    for json_path in new_jsons:
        baseline_path = baseline_run / json_path.name
        if not baseline_path.exists():
            print(
                f"\n## {json_path.stem}\n\n(no matching file in {baseline_run.name}, skipping)"
            )
            continue
        # process_benchmark's first arg is the "new" run, second is the baseline
        # (it computes (new - baseline) / baseline). Match analyze_results.py exactly.
        df = process_benchmark(json_path, baseline_path)
        print(f"\n## {json_path.stem}\n")
        print(df.to_markdown(index=False))


if __name__ == "__main__":
    main()
