# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Calibrate one arch's baseline YAML by running the microbenchmark binary N
times locally on its dedicated runner.

Runs the bench N times on the machine it's invoked on, takes one median per
run, and derives `median_throughput` + `tolerance_pct` per (test, case) via
MAD K=4.5. Designed for dedicated self-hosted runners where the across-run
noise floor is small and a handful of invocations is enough to size
tolerances accurately.

Output: a single `<arch_slug>.yaml` in `tests/microbenchmark/expected/baselines/`,
identified by the runner's hostname (or `--arch` override). Other arch files
in that directory are untouched.

Usage on `bgd-lab-06` (WH n150):

    python3 tests/microbenchmark/tools/calibrate_on_runner.py --iters 10

Override the arch if needed (smoke tests, calibrating multiple archs from
one host, etc.):

    python3 tests/microbenchmark/tools/calibrate_on_runner.py --iters 5 \\
        --arch 'WH n300'

Dry-run shows the diff against the existing YAML without writing:

    python3 tests/microbenchmark/tools/calibrate_on_runner.py --dry-run
"""

import argparse
import io
import math
import os
import socket
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict
from datetime import date
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
from summarize_regressions import read_arch_results  # noqa: E402

# --- Tolerance math -------------------------------------------------------------
#
# Tolerance band = max(5, ceil(MAD_K * mad_pct)). MAD_K = 4.5 ≈ 3σ (since
# σ ≈ 1.4826 × MAD for roughly-normal data), so the band covers ~99.7 % of
# single-sample observations from the same distribution — i.e. one new CI run
# vs the calibrated median. The 5 % floor stops near-deterministic tests from
# getting a 0 % tolerance that would alert on rounding noise. MAD itself is
# robust against single-iteration outliers (one bad sample shifts MAD by at
# most one rank), unlike stdev which squares deviations.

MIN_SAMPLES_FOR_RELIABLE = 10
TOLERANCE_FLOOR_PCT = 5
MAD_K = 4.5


def arch_slug(arch_label: str) -> str:
    """Canonical filename slug for a given arch label.
    "WH n150" -> "wh_n150", "BH p150b" -> "bh_p150b".
    """
    return arch_label.lower().replace(" ", "_")


def derive_baseline_entry(samples: list[float]) -> tuple[float, float, str]:
    """Return (median_throughput, tolerance_pct, comment).

    `comment` is a free-text suffix (may be empty) appended to the YAML line.
    """
    if len(samples) < MIN_SAMPLES_FOR_RELIABLE:
        suffix = f"  # only {len(samples)} samples — tolerance may be too tight/loose"
    else:
        suffix = ""
    median_throughput = statistics.median(samples)
    if len(samples) < 2:
        # No spread to measure with one sample — use the floor.
        return median_throughput, float(TOLERANCE_FLOOR_PCT), suffix
    mad = statistics.median(abs(s - median_throughput) for s in samples)
    mad_pct = mad / median_throughput * 100 if median_throughput else 0.0
    tolerance_pct = max(TOLERANCE_FLOOR_PCT, math.ceil(MAD_K * mad_pct))
    return median_throughput, float(tolerance_pct), suffix


def _yaml_escape(s: str) -> str:
    """Wrap a string in quotes if it contains characters that confuse YAML."""
    if any(c in s for c in ":#,&*!|>'\"%@`{}[]\n") or s.strip() != s:
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


# Hostname -> (arch label, yaml filename). New runners added here as they come
# online. The yaml filename is derivable from the arch label via arch_slug(),
# but keeping it explicit makes the mapping self-documenting and overridable.
RUNNER_ARCH_MAP: dict[str, tuple[str, str]] = {
    "bgd-lab-06": ("WH n150", "wh_n150.yaml"),
    "bgd-lab-05": ("WH n300", "wh_n300.yaml"),
    "bh-40": ("BH p150b", "bh_p150b.yaml"),
}

# Same path the CI workflow invokes (see .github/workflows/run-benchmarks.yml).
BENCH_CMD_DEFAULT = "./build/test/umd/microbenchmark/umd_microbenchmark"
BASELINES_DIR_DEFAULT = Path(__file__).resolve().parents[1] / "expected" / "baselines"


# --- Helpers ---------------------------------------------------------------------


def resolve_arch(arch_arg: str | None, hostname: str) -> tuple[str, str]:
    """Return (arch_label, yaml_filename) for this calibration run.

    Priority: explicit --arch flag (looked up against the canonical labels in
    RUNNER_ARCH_MAP) > hostname-based detection. Exits non-zero with a clear
    message if neither resolves.
    """
    if arch_arg:
        # Find the entry for this arch label, regardless of hostname.
        for label, filename in RUNNER_ARCH_MAP.values():
            if label == arch_arg:
                return label, filename
        # Allow a label not yet in RUNNER_ARCH_MAP — derive filename from slug.
        return arch_arg, f"{arch_slug(arch_arg)}.yaml"
    if hostname in RUNNER_ARCH_MAP:
        return RUNNER_ARCH_MAP[hostname]
    known = ", ".join(sorted(RUNNER_ARCH_MAP.keys())) or "(none configured)"
    sys.exit(
        f"Hostname {hostname!r} is not in RUNNER_ARCH_MAP (known: {known}). "
        f"Pass --arch '<WH n150|WH n300|BH p150b>' to override."
    )


def run_bench_iters(
    bench_cmd: str,
    gtest_filter: str | None,
    iters: int,
    results_root: Path,
) -> list[Path]:
    """Run the benchmark binary `iters` times, each with a unique
    UMD_MICROBENCHMARK_RESULTS_PATH. Returns the list of iter directories.

    Aborts the script on any failed invocation — partial calibrations would
    silently bias toward the iterations that happened to succeed.
    """
    iter_dirs: list[Path] = []
    base_env = os.environ.copy()
    cmd_parts = [bench_cmd]
    if gtest_filter:
        cmd_parts.append(f"--gtest_filter={gtest_filter}")
    for i in range(iters):
        iter_dir = results_root / f"iter_{i:03d}"
        iter_dir.mkdir(parents=True, exist_ok=True)
        env = {**base_env, "UMD_MICROBENCHMARK_RESULTS_PATH": str(iter_dir)}
        print(
            f"[{i + 1}/{iters}] running: {' '.join(cmd_parts)} "
            f"(UMD_MICROBENCHMARK_RESULTS_PATH={iter_dir})",
            file=sys.stderr,
        )
        result = subprocess.run(cmd_parts, env=env, capture_output=True, text=True)
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            sys.exit(
                f"\nBench exited {result.returncode} on iter {i}. "
                f"Aborting — calibration is not partial-safe."
            )
        iter_dirs.append(iter_dir)
    return iter_dirs


def aggregate_samples(iter_dirs: list[Path], arch_label: str) -> dict:
    """Combine per-iter benchmark outputs into per-(title, case) sample lists.

    Returns: { title: { case: [throughput_iter0, throughput_iter1, ...] } }
    """
    samples: dict = defaultdict(lambda: defaultdict(list))
    for iter_dir in iter_dirs:
        per_arch = read_arch_results(iter_dir, arch_label)
        for title, cases in per_arch.items():
            for case_name, entry in cases.items():
                samples[title][case_name].append(entry["throughput"])
    return samples


def load_existing_arch_yaml(path: Path) -> dict:
    """Load a per-arch YAML if it exists; return {} otherwise."""
    if not path.exists():
        return {}
    with open(path) as f:
        return yaml.safe_load(f) or {}


def render_arch_yaml(
    arch: str,
    runner_hostname: str,
    samples_for_arch: dict,
    existing: dict,
    iters: int,
) -> tuple[str, list, list, list]:
    """Render the per-arch YAML text from local-bench samples.

    Returns:
        (text, change_lines, low_sample_lines, skipped_new_cases)

    `change_lines` lists every case whose median or tolerance moved vs the
    existing file (one line each).
    `low_sample_lines` flags any case with fewer than MIN_SAMPLES_FOR_RELIABLE
    iterations (should not happen if iters >= 10 and bench succeeded, but is
    logged defensively in case a single test crashed or was skipped).
    `skipped_new_cases` lists cases present in samples but not in the existing
    YAML — the script does not insert them automatically.
    """
    change_lines: list[str] = []
    low_sample_lines: list[str] = []
    skipped_new_cases: list[str] = []

    existing_tests = {t: cases for t, cases in existing.items() if t != "metadata"}

    out = io.StringIO()
    slug = arch_slug(arch)
    out.write(f"# tests/microbenchmark/expected/baselines/{slug}.yaml\n")
    out.write(
        f"# Calibrated for {arch} on dedicated runner {runner_hostname!r} from "
        f"{iters} local bench invocations.\n"
    )
    out.write(
        "# To recalibrate: tests/microbenchmark/tools/calibrate_on_runner.py "
        f"(must be run on {runner_hostname}).\n"
    )
    out.write("#\n")
    out.write("# Schema (no in-file arch label — the file is dedicated to one arch):\n")
    out.write(
        "#   <bench_title>:\n"
        "#     <case_name>: { median_throughput: <float>, tolerance_pct: <float>[, gate: true] }\n"
    )
    out.write("\n")
    out.write("metadata:\n")
    out.write("  schema_version: 1\n")
    out.write(f'  arch: "{arch}"\n')
    out.write(f'  runner_hostname: "{runner_hostname}"\n')
    out.write(f'  calibrated_at: "{date.today().isoformat()}"\n')
    out.write(f"  calibrated_from_runs: {iters}\n")
    out.write(
        '  notes: "Calibrated locally via tests/microbenchmark/tools/calibrate_on_runner.py."\n'
    )
    out.write("\n")

    # Iterate tests in the order they appear in the existing YAML so the diff
    # is readable. New tests (in samples but not the YAML) would normally be
    # appended here, but the script skips them and warns instead — adding a
    # case to baselines should be an intentional review action, not a side
    # effect of running calibration.
    seen_test_titles: set = set()
    for title, existing_cases in existing_tests.items():
        if not isinstance(existing_cases, dict):
            continue
        seen_test_titles.add(title)
        case_lines: list[str] = []
        for case_name, existing_entry in existing_cases.items():
            if not isinstance(existing_entry, dict):
                continue
            new_samples = samples_for_arch.get(title, {}).get(case_name)
            if not new_samples:
                # Missing from this calibration — keep the existing values
                # rather than dropping the row.
                median_throughput = existing_entry.get("median_throughput")
                tolerance_pct = existing_entry.get("tolerance_pct")
                if median_throughput is None or tolerance_pct is None:
                    print(
                        f"WARN: {title}/{case_name}: no samples and existing "
                        f"entry is incomplete; skipping.",
                        file=sys.stderr,
                    )
                    continue
                change_lines.append(
                    f"  {title} :: {case_name}: no samples collected — "
                    f"keeping existing median {median_throughput:.4g}, "
                    f"tolerance ±{tolerance_pct:g}%"
                )
                suffix = "  # no new samples this calibration"
            else:
                median_throughput, tolerance_pct, suffix = derive_baseline_entry(
                    new_samples
                )
                if len(new_samples) < MIN_SAMPLES_FOR_RELIABLE:
                    low_sample_lines.append(
                        f"  {title} :: {case_name}: {len(new_samples)} sample(s)"
                    )
                old_median = existing_entry.get("median_throughput")
                old_tolerance = existing_entry.get("tolerance_pct")
                change_lines.append(
                    f"  {title} :: {case_name}: "
                    f"median {old_median:.4g} -> {median_throughput:.4g}, "
                    f"tolerance ±{old_tolerance:g}% -> ±{tolerance_pct:g}%"
                )
            gate = existing_entry.get("gate") is True
            gate_str = ", gate: true" if gate else ""
            case_lines.append(
                f"  {_yaml_escape(case_name)}: "
                f"{{ median_throughput: {median_throughput:.4g}, "
                f"tolerance_pct: {tolerance_pct:g}{gate_str} }}{suffix}"
            )
        if case_lines:
            out.write(f"{title}:\n")
            for line in case_lines:
                out.write(line + "\n")
            out.write("\n")

    # Cases present in samples but not in the existing YAML — flag for human
    # follow-up rather than auto-inserting.
    for title, cases in samples_for_arch.items():
        for case_name in cases:
            existing_cases = existing_tests.get(title)
            if not isinstance(existing_cases, dict) or case_name not in existing_cases:
                skipped_new_cases.append(f"  {title} :: {case_name}")

    return out.getvalue(), change_lines, low_sample_lines, skipped_new_cases


# --- Main ------------------------------------------------------------------------


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--iters",
        type=int,
        default=10,
        help="Number of bench invocations to sample. Must be >= 2 (default: 10).",
    )
    p.add_argument(
        "--bench-cmd",
        default=BENCH_CMD_DEFAULT,
        help=f"Benchmark binary path (default: {BENCH_CMD_DEFAULT}).",
    )
    p.add_argument(
        "--gtest-filter",
        default=None,
        help="Optional --gtest_filter expression passed through to the binary.",
    )
    p.add_argument(
        "--arch",
        default=None,
        help=(
            "Arch label override (e.g. 'WH n150'). "
            "Default: derived from hostname via RUNNER_ARCH_MAP."
        ),
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
        "--results-root",
        type=Path,
        default=None,
        help="Where to put per-iter benchmark outputs (default: a fresh temp dir).",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the diff but do not write the YAML.",
    )
    args = p.parse_args()

    if args.iters < 2:
        sys.exit(f"--iters must be >= 2 (got {args.iters})")

    hostname = socket.gethostname()
    arch_label, yaml_filename = resolve_arch(args.arch, hostname)
    print(f"Hostname: {hostname}", file=sys.stderr)
    print(f"Arch:     {arch_label}", file=sys.stderr)
    print(f"Output:   {args.baselines_dir / yaml_filename}", file=sys.stderr)

    # Where to land per-iter benchmark outputs.
    if args.results_root is None:
        results_root = Path(
            tempfile.mkdtemp(prefix=f"calibrate_{arch_slug(arch_label)}_")
        )
    else:
        results_root = args.results_root
        results_root.mkdir(parents=True, exist_ok=True)
    print(f"Results:  {results_root}", file=sys.stderr)

    iter_dirs = run_bench_iters(
        args.bench_cmd, args.gtest_filter, args.iters, results_root
    )

    samples = aggregate_samples(iter_dirs, arch_label)
    if not samples:
        sys.exit(
            f"No samples collected after {args.iters} bench invocations. "
            f"Check that {args.bench_cmd} writes JSON to "
            f"$UMD_MICROBENCHMARK_RESULTS_PATH."
        )

    out_path = args.baselines_dir / yaml_filename
    existing = load_existing_arch_yaml(out_path)
    yaml_text, change_lines, low_sample_lines, skipped_new = render_arch_yaml(
        arch_label, hostname, samples, existing, args.iters
    )

    if args.dry_run:
        print(
            "--- dry-run: would write the following to " f"{out_path} ---",
            file=sys.stderr,
        )
        sys.stdout.write(yaml_text)
    else:
        args.baselines_dir.mkdir(parents=True, exist_ok=True)
        out_path.write_text(yaml_text)
        print(f"\nWrote {out_path}", file=sys.stderr)

    if change_lines:
        print(f"\nChanges ({len(change_lines)}):", file=sys.stderr)
        for line in change_lines:
            print(line, file=sys.stderr)
    if low_sample_lines:
        print(
            f"\n{len(low_sample_lines)} case(s) calibrated from fewer than "
            f"{MIN_SAMPLES_FOR_RELIABLE} samples (review before committing):",
            file=sys.stderr,
        )
        for line in low_sample_lines:
            print(line, file=sys.stderr)
    if skipped_new:
        print(
            f"\n{len(skipped_new)} new case(s) seen in samples but absent from "
            f"{out_path.name}; add manually if intended:",
            file=sys.stderr,
        )
        for line in skipped_new:
            print(line, file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
