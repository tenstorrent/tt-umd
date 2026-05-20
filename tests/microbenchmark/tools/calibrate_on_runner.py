# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Calibrate one arch's baseline YAML from a single microbenchmark invocation
on its dedicated runner.

Runs the bench once on the machine it's invoked on, reads
`median(elapsed)` + `medianAbsolutePercentError(elapsed)` per case from the
nanobench JSON, and writes:
    median_throughput = batch / median(elapsed)
    tolerance_pct     = max(5, ceil(MAPE_K * mape_pct))

`mape_pct` is the within-run epoch spread reported by nanobench, already as a
percent of the median. MAPE_K=1 means "tolerance equals the observed
within-run spread". If CI's run-to-run noise turns out to exceed within-run
noise (cold-cache or between-process effects), raise MAPE_K — K≈2.5 covers
~3σ of a median-vs-median comparison under normal-distribution assumptions.

Output: a single `<arch_slug>.yaml` in `tests/microbenchmark/expected/baselines/`,
identified by the runner's hostname (or `--arch` override). Other arch files
in that directory are untouched. Existing `gate: true` flags are preserved.

Usage on `bgd-lab-06` (WH n150):

    python3 tests/microbenchmark/tools/calibrate_on_runner.py

Override the arch if needed (smoke tests, calibrating from a different host):

    python3 tests/microbenchmark/tools/calibrate_on_runner.py --arch 'WH n300'

Dry-run shows what would be written without modifying the YAML:

    python3 tests/microbenchmark/tools/calibrate_on_runner.py --dry-run
"""

import argparse
import io
import json
import math
import os
import re
import socket
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path

import yaml

# --- Tolerance math -------------------------------------------------------------
#
# Tolerance band = max(5, ceil(MAPE_K * mape_pct)).
#
# mape_pct is nanobench's medianAbsolutePercentError(elapsed) for a single
# run: the spread of epoch medians within that run, expressed as a percent of
# the median. MAPE_K=1 sizes tolerance to equal that observed spread.
#
# Bump MAPE_K up if CI's run-to-run variance turns out larger than within-run
# variance (likely on shared/noisy hosts; less likely on a clean dedicated
# runner with appropriately tuned epoch counts).

TOLERANCE_FLOOR_PCT = 5
MAPE_K = 1

# nanobench renders unset floating-point fields as bare `-nan`/`nan`, which
# Python's json parser rejects (it only accepts the capitalized `NaN`).
_NAN_RE = re.compile(r"-?\bnan\b")


def arch_slug(arch_label: str) -> str:
    """Canonical filename slug for a given arch label.
    "WH n150" -> "wh_n150", "BH p150b" -> "bh_p150b".
    """
    return arch_label.lower().replace(" ", "_")


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

    Priority: explicit --arch flag > hostname-based detection. Exits non-zero
    with a clear message if neither resolves.
    """
    if arch_arg:
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


def run_bench(
    bench_cmd: str,
    gtest_filter: str | None,
    results_dir: Path,
) -> None:
    """Run the benchmark binary once with UMD_MICROBENCHMARK_RESULTS_PATH set
    to `results_dir`. Aborts on non-zero exit.
    """
    cmd_parts = [bench_cmd]
    if gtest_filter:
        cmd_parts.append(f"--gtest_filter={gtest_filter}")
    env = {**os.environ, "UMD_MICROBENCHMARK_RESULTS_PATH": str(results_dir)}
    print(
        f"running: {' '.join(cmd_parts)} "
        f"(UMD_MICROBENCHMARK_RESULTS_PATH={results_dir})",
        file=sys.stderr,
    )
    result = subprocess.run(cmd_parts, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        sys.exit(f"\nBench exited {result.returncode}. Aborting.")


def derive_entry(
    median_elapsed: float, batch: float, mape_pct: float
) -> tuple[float, float]:
    """Compute (median_throughput, tolerance_pct) from one nanobench result.

    `mape_pct` is the percent-of-median spread (already converted from
    nanobench's fraction form). Returns the entry that lands in the YAML.
    """
    median_throughput = batch / median_elapsed
    tolerance_pct = max(TOLERANCE_FLOOR_PCT, math.ceil(MAPE_K * mape_pct))
    return median_throughput, float(tolerance_pct)


def collect_entries(results_dir: Path) -> dict:
    """Read every `<title>.json` in `results_dir` and return:
    { title: { case_name: (median_throughput, tolerance_pct) } }
    """
    entries: dict = {}
    for path in sorted(results_dir.glob("*.json")):
        title = path.stem
        if title == "machine_host_spec":
            continue
        try:
            text = _NAN_RE.sub("NaN", path.read_text())
            data = json.loads(text)
        except (json.JSONDecodeError, OSError) as e:
            print(f"WARN: skipping {path}: {e}", file=sys.stderr)
            continue
        for result in data.get("results", []):
            case = result.get("name")
            med = result.get("median(elapsed)")
            batch = result.get("batch")
            if case is None or med is None or batch is None or med == 0 or batch == 0:
                continue
            # nanobench stores MAPE as a fraction (0.03 == 3%); convert to %.
            mape_pct = (result.get("medianAbsolutePercentError(elapsed)") or 0.0) * 100
            entries.setdefault(title, {})[case] = derive_entry(med, batch, mape_pct)
    return entries


def load_existing_arch_yaml(path: Path) -> dict:
    """Load a per-arch YAML if it exists; return {} otherwise."""
    if not path.exists():
        return {}
    with open(path) as f:
        return yaml.safe_load(f) or {}


def render_arch_yaml(
    arch: str,
    runner_hostname: str,
    new_entries: dict,
    existing: dict,
) -> tuple[str, list, list]:
    """Render the per-arch YAML text from one bench run's results.

    Returns:
        (text, change_lines, skipped_new_cases)

    `new_entries` is { title: { case: (median_throughput, tolerance_pct) } } as
    produced by `collect_entries`.

    `change_lines` lists every case whose median or tolerance moved vs the
    existing YAML. `skipped_new_cases` lists cases present in this run but not
    in the existing YAML — the script does not insert them automatically.
    """
    change_lines: list[str] = []
    skipped_new_cases: list[str] = []

    existing_tests = {t: cases for t, cases in existing.items() if t != "metadata"}

    out = io.StringIO()
    slug = arch_slug(arch)
    out.write(f"# tests/microbenchmark/expected/baselines/{slug}.yaml\n")
    out.write(
        f"# Calibrated for {arch} on dedicated runner {runner_hostname!r} "
        "from a single bench invocation.\n"
    )
    out.write(
        "# To recalibrate: tests/microbenchmark/tools/calibrate_on_runner.py "
        f"(must be run on {runner_hostname}).\n"
    )
    out.write("#\n")
    out.write("# Schema (no in-file arch label — the file is dedicated to one arch):\n")
    out.write(
        "#   <bench_title>:\n"
        "#     <case_name>: { median_throughput: <float>, "
        "tolerance_pct: <float>[, gate: true] }\n"
    )
    out.write("\n")
    out.write("metadata:\n")
    out.write("  schema_version: 1\n")
    out.write(f'  arch: "{arch}"\n')
    out.write(f'  runner_hostname: "{runner_hostname}"\n')
    out.write(f'  calibrated_at: "{date.today().isoformat()}"\n')
    out.write(
        '  notes: "Calibrated locally via tests/microbenchmark/tools/calibrate_on_runner.py."\n'
    )
    out.write("\n")

    # Iterate tests in the order they appear in the existing YAML so the diff
    # is readable. New tests (in this run's results but not the YAML) are
    # collected as skipped_new_cases for human follow-up — adding a case to
    # baselines should be an intentional review action, not a side effect.
    for title, existing_cases in existing_tests.items():
        if not isinstance(existing_cases, dict):
            continue
        case_lines: list[str] = []
        for case_name, existing_entry in existing_cases.items():
            if not isinstance(existing_entry, dict):
                continue
            new = new_entries.get(title, {}).get(case_name)
            if not new:
                # Missing from this run — keep existing values.
                median_throughput = existing_entry.get("median_throughput")
                tolerance_pct = existing_entry.get("tolerance_pct")
                if median_throughput is None or tolerance_pct is None:
                    print(
                        f"WARN: {title}/{case_name}: no result and existing "
                        f"entry is incomplete; skipping.",
                        file=sys.stderr,
                    )
                    continue
                change_lines.append(
                    f"  {title} :: {case_name}: no result — "
                    f"keeping existing median {median_throughput:.4g}, "
                    f"tolerance ±{tolerance_pct:g}%"
                )
                suffix = "  # no new result this calibration"
            else:
                median_throughput, tolerance_pct = new
                old_median = existing_entry.get("median_throughput")
                old_tolerance = existing_entry.get("tolerance_pct")
                change_lines.append(
                    f"  {title} :: {case_name}: "
                    f"median {old_median:.4g} -> {median_throughput:.4g}, "
                    f"tolerance ±{old_tolerance:g}% -> ±{tolerance_pct:g}%"
                )
                suffix = ""
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

    # Cases present in this run but not in the existing YAML — flag.
    for title, cases in new_entries.items():
        for case_name in cases:
            existing_cases = existing_tests.get(title)
            if not isinstance(existing_cases, dict) or case_name not in existing_cases:
                skipped_new_cases.append(f"  {title} :: {case_name}")

    return out.getvalue(), change_lines, skipped_new_cases


# --- Main ------------------------------------------------------------------------


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
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
        help=f"Directory of per-arch baseline YAMLs (default: {BASELINES_DIR_DEFAULT}).",
    )
    p.add_argument(
        "--results-dir",
        type=Path,
        default=None,
        help="Where to put benchmark JSON outputs (default: a fresh temp dir).",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the diff but do not write the YAML.",
    )
    args = p.parse_args()

    hostname = socket.gethostname()
    arch_label, yaml_filename = resolve_arch(args.arch, hostname)
    print(f"Hostname: {hostname}", file=sys.stderr)
    print(f"Arch:     {arch_label}", file=sys.stderr)
    print(f"Output:   {args.baselines_dir / yaml_filename}", file=sys.stderr)

    if args.results_dir is None:
        results_dir = Path(
            tempfile.mkdtemp(prefix=f"calibrate_{arch_slug(arch_label)}_")
        )
    else:
        results_dir = args.results_dir
        results_dir.mkdir(parents=True, exist_ok=True)
    print(f"Results:  {results_dir}", file=sys.stderr)

    run_bench(args.bench_cmd, args.gtest_filter, results_dir)

    new_entries = collect_entries(results_dir)
    if not new_entries:
        sys.exit(
            f"No results collected from {results_dir}. "
            f"Check that {args.bench_cmd} writes JSON to "
            f"$UMD_MICROBENCHMARK_RESULTS_PATH."
        )

    out_path = args.baselines_dir / yaml_filename
    existing = load_existing_arch_yaml(out_path)
    yaml_text, change_lines, skipped_new = render_arch_yaml(
        arch_label, hostname, new_entries, existing
    )

    if args.dry_run:
        print(
            f"--- dry-run: would write the following to {out_path} ---",
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
    if skipped_new:
        print(
            f"\n{len(skipped_new)} new case(s) seen in this run but absent from "
            f"{out_path.name}; add manually if intended:",
            file=sys.stderr,
        )
        for line in skipped_new:
            print(line, file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
