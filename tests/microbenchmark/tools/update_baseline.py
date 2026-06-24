# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Update one arch's baseline YAML from a directory of nanobench JSON outputs.

Reads `median(elapsed)` + `medianAbsolutePercentError(elapsed)` per case from
each `<title>.json` in the supplied directory and writes:
    median_value = batch / median(elapsed)
    tolerance_pct     = max(5, ceil(MAPE_K * mape_pct))
into the per-arch YAML under `tests/microbenchmark/baselines/`.

`mape_pct` is the within-run epoch spread reported by nanobench (already a
percent of the median). MAPE_K=1 means "tolerance equals the observed
within-run spread". If CI's run-to-run noise turns out to exceed within-run
noise (cold-cache or between-process effects), raise MAPE_K — K≈2.5 covers
~3σ of a median-vs-median comparison under normal-distribution assumptions.

Typical workflow: a CI run produced a `benchmark-json-<arch>-...` artifact;
download it and point this script at the result.

Example:

    gh run download <run-id> \\
        --name benchmark-json-wormhole_b0-n150-umd-perf-ubuntu-22.04 \\
        --dir /tmp/ci-bench

    python3 tests/microbenchmark/tools/update_baseline.py \\
        --arch 'n150' --from-results-dir /tmp/ci-bench

    # inspect the diff, commit n150.yaml.

Dry-run prints what would be written without modifying the YAML:

    python3 tests/microbenchmark/tools/update_baseline.py \\
        --arch 'n150' --from-results-dir /tmp/ci-bench --dry-run

All cases present in the supplied results are written to the YAML. Existing
`gate: true` flags are preserved when overwriting. Cases present in the
existing baseline but absent from the supplied results keep their old values
(with a `# no new result this calibration` annotation). To exclude a case
from regression-check, delete its line from the YAML by hand — it'll come
back the next time you point this script at a CI artifact that includes it.
"""

import argparse
import io
import json
import math
import re
import sys
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
# variance.

TOLERANCE_FLOOR_PCT = 5
MAPE_K = 1

# nanobench renders unset floating-point fields as bare `-nan`/`nan`, which
# Python's json parser rejects (it only accepts the capitalized `NaN`).
_NAN_RE = re.compile(r"-?\bnan\b")

# Arch label -> (yaml filename, dedicated runner hostname). Used to find the
# destination file under baselines/ and to fill in the runner_hostname
# metadata field that records which machine produced the JSON output.
ARCH_RUNNERS: dict[str, tuple[str, str]] = {
    "n150": ("n150.yaml", "bgd-lab-06"),
    "n300": ("n300.yaml", "bgd-lab-05"),
    "p150": ("p150.yaml", "bh-40"),
}

BASELINES_DIR_DEFAULT = Path(__file__).resolve().parents[1] / "baselines"


def _yaml_escape(s: str) -> str:
    """Wrap a string in quotes if it contains characters that confuse YAML."""
    if any(c in s for c in ":#,&*!|>'\"%@`{}[]\n") or s.strip() != s:
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


def derive_entry(
    median_elapsed: float, batch: float, mape_pct: float
) -> tuple[float, float]:
    """Compute (median_value, tolerance_pct) from one nanobench result.

    `mape_pct` is the percent-of-median spread (already converted from
    nanobench's fraction form). Returns the entry that lands in the YAML.
    """
    median_value = batch / median_elapsed
    tolerance_pct = max(TOLERANCE_FLOOR_PCT, math.ceil(MAPE_K * mape_pct))
    return median_value, float(tolerance_pct)


def collect_entries(results_dir: Path) -> dict:
    """Read every `<title>.json` in `results_dir` and return:
    { title: { case_name: (median_value, tolerance_pct) } }
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
            if not (math.isfinite(med) and math.isfinite(batch)):
                print(
                    f"WARN: skipping {title}/{case}: non-finite median/batch",
                    file=sys.stderr,
                )
                continue
            # nanobench stores MAPE as a fraction (0.03 == 3%); convert to %.
            raw_mape = result.get("medianAbsolutePercentError(elapsed)")
            mape_pct = (
                raw_mape * 100
                if raw_mape is not None and math.isfinite(raw_mape)
                else 0.0
            )
            entries.setdefault(title, {})[case] = derive_entry(med, batch, mape_pct)
    return entries


def load_existing_arch_yaml(path: Path) -> dict:
    """Load a per-arch YAML if it exists; return {} otherwise."""
    if not path.exists():
        return {}
    return yaml.safe_load(path.read_text()) or {}


def render_arch_yaml(
    arch: str,
    yaml_filename: str,
    runner_hostname: str,
    new_entries: dict,
    existing: dict,
) -> tuple[str, list]:
    """Render the per-arch YAML text from one nanobench JSON output.

    Returns:
        (text, change_lines)

    `new_entries` is { title: { case: (median_value, tolerance_pct) } } as
    produced by `collect_entries`.

    Every case in `new_entries` is written to the output. Cases present in
    `existing` but absent from `new_entries` are also written, with their old
    values preserved and a `# no new result this calibration` annotation.
    `gate: true` flags on the existing entry are preserved when overwriting.
    To exclude a case from regression-check, delete its line from the YAML
    by hand — it'll reappear the next time this script runs against an
    artifact that contains it.
    """
    change_lines: list[str] = []

    existing_tests = {t: cases for t, cases in existing.items() if t != "metadata"}

    out = io.StringIO()
    out.write(f"# tests/microbenchmark/baselines/{yaml_filename}\n")
    out.write(
        f"# Calibrated for {arch} from a single bench invocation on dedicated "
        f"runner {runner_hostname!r}.\n"
    )
    out.write(
        "# To recalibrate: download a benchmark-json-* artifact from a CI run\n"
        "# of build-and-run-all-benchmarks.yml and pass its directory to\n"
        "# tests/microbenchmark/tools/update_baseline.py via --from-results-dir.\n"
    )
    out.write("#\n")
    out.write("# Schema (no in-file arch label — the file is dedicated to one arch):\n")
    out.write(
        "#   <bench_title>:\n"
        "#     <case_name>: { median_value: <float>, "
        "tolerance_pct: <float>[, gate: true] }\n"
    )
    out.write("\n")
    out.write("metadata:\n")
    out.write("  schema_version: 1\n")
    out.write(f'  arch: "{arch}"\n')
    out.write(f'  runner_hostname: "{runner_hostname}"\n')
    out.write(f'  calibrated_at: "{date.today().isoformat()}"\n')
    out.write(
        '  notes: "Updated from a CI benchmark JSON artifact via '
        'tests/microbenchmark/tools/update_baseline.py."\n'
    )
    out.write("\n")

    # Build an ordered list of (title, case) pairs covering the union of what's
    # in the new results and what's in the existing YAML. new_entries order
    # comes first (so the output reflects nanobench/source-code order); any
    # case that's in YAML-only (no new result) is appended afterward to its
    # test section so the kept-value annotation is visible alongside its peers.
    seen: set = set()
    test_to_cases: dict[str, list[str]] = {}
    for title, cases in new_entries.items():
        for case_name in cases:
            if (title, case_name) in seen:
                continue
            test_to_cases.setdefault(title, []).append(case_name)
            seen.add((title, case_name))
    for title, existing_cases in existing_tests.items():
        if not isinstance(existing_cases, dict):
            continue
        for case_name in existing_cases:
            if (title, case_name) in seen:
                continue
            test_to_cases.setdefault(title, []).append(case_name)
            seen.add((title, case_name))

    for title, case_names in test_to_cases.items():
        case_lines: list[str] = []
        for case_name in case_names:
            new = new_entries.get(title, {}).get(case_name)
            existing_entry = existing_tests.get(title, {}).get(case_name)
            existing_entry = (
                existing_entry if isinstance(existing_entry, dict) else None
            )
            gate = existing_entry is not None and existing_entry.get("gate") is True
            gate_str = ", gate: true" if gate else ""

            if new is not None:
                median_value, tolerance_pct = new
                if existing_entry is not None:
                    old_median = existing_entry.get("median_value")
                    old_tolerance = existing_entry.get("tolerance_pct")
                    if old_median is not None and old_tolerance is not None:
                        change_lines.append(
                            f"  {title} :: {case_name}: "
                            f"median {old_median:.4g} -> {median_value:.4g}, "
                            f"tolerance ±{old_tolerance:g}% -> ±{tolerance_pct:g}%"
                        )
                    else:
                        change_lines.append(
                            f"  {title} :: {case_name}: new entry "
                            f"(median {median_value:.4g}, tolerance ±{tolerance_pct:g}%)"
                        )
                else:
                    change_lines.append(
                        f"  {title} :: {case_name}: new entry "
                        f"(median {median_value:.4g}, tolerance ±{tolerance_pct:g}%)"
                    )
                suffix = ""
            else:
                # Case is in the existing YAML but not in this run's results —
                # keep the old values so a partial recalibration doesn't drop
                # entries we just don't have fresh numbers for.
                if existing_entry is None:
                    continue
                median_value = existing_entry.get("median_value")
                tolerance_pct = existing_entry.get("tolerance_pct")
                if median_value is None or tolerance_pct is None:
                    print(
                        f"WARN: {title}/{case_name}: no result and existing "
                        f"entry is incomplete; skipping.",
                        file=sys.stderr,
                    )
                    continue
                change_lines.append(
                    f"  {title} :: {case_name}: no result — "
                    f"keeping existing median {median_value:.4g}, "
                    f"tolerance ±{tolerance_pct:g}%"
                )
                suffix = "  # no new result this calibration"

            case_lines.append(
                f"  {_yaml_escape(case_name)}: "
                f"{{ median_value: {median_value:.4g}, "
                f"tolerance_pct: {tolerance_pct:g}{gate_str} }}{suffix}"
            )
        if case_lines:
            out.write(f"{title}:\n")
            for line in case_lines:
                out.write(line + "\n")
            out.write("\n")

    return out.getvalue(), change_lines


# --- Main ------------------------------------------------------------------------


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--arch",
        required=True,
        choices=list(ARCH_RUNNERS.keys()),
        help="Arch label whose baseline YAML to update.",
    )
    p.add_argument(
        "--from-results-dir",
        type=Path,
        required=True,
        help=(
            "Directory holding nanobench `<title>.json` outputs to consume "
            "(e.g. a downloaded benchmark-json-* CI artifact)."
        ),
    )
    p.add_argument(
        "--baselines-dir",
        type=Path,
        default=BASELINES_DIR_DEFAULT,
        help=f"Directory of per-arch baseline YAMLs (default: {BASELINES_DIR_DEFAULT}).",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the would-be YAML without writing it.",
    )
    args = p.parse_args()

    yaml_filename, runner_hostname = ARCH_RUNNERS[args.arch]

    try:
        results_dir = args.from_results_dir.resolve(strict=True)
    except FileNotFoundError:
        sys.exit(f"--from-results-dir does not exist: {args.from_results_dir}")
    if not results_dir.is_dir():
        sys.exit(f"--from-results-dir is not a directory: {results_dir}")

    out_path = args.baselines_dir / yaml_filename
    print(f"Arch:     {args.arch}", file=sys.stderr)
    print(f"Source:   {results_dir}", file=sys.stderr)
    print(f"Output:   {out_path}", file=sys.stderr)

    new_entries = collect_entries(results_dir)
    if not new_entries:
        sys.exit(
            f"No usable nanobench results found in {results_dir}. "
            "Expected one or more `<title>.json` files containing a `results` array."
        )

    existing = load_existing_arch_yaml(out_path)
    yaml_text, change_lines = render_arch_yaml(
        args.arch, yaml_filename, runner_hostname, new_entries, existing
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

    return 0


if __name__ == "__main__":
    sys.exit(main())
