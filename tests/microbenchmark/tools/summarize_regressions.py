# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Cross-architecture regression summary against in-repo per-arch baselines.

Reads benchmark JSON artifacts produced by `run-benchmarks.yml` (one artifact
directory per architecture, each containing nanobench JSONs) and compares each
case's throughput against the stored `median_value` from the per-arch
YAMLs in `tests/microbenchmark/baselines/`. Emits markdown with:

  1. A coarse cross-arch table (one row per test, one column per arch),
     reporting only breach counts per severity tier — never an aggregated Δ%.
  2. Per-(test, arch) detail sections for cells with at least one breach,
     listing only the breached cases.

Sister workflow: `analyze_results.py` (per-arch drift-from-latest-main). This
script is complementary, not a replacement.
"""

import argparse
import json
import math
import re
import sys
from collections import defaultdict
from pathlib import Path

import yaml

from utils import format_throughput

# nanobench renders unset floating-point fields as bare `-nan`/`nan`, which
# Python's json parser rejects (it only accepts the capitalized `NaN`).
_NAN_RE = re.compile(r"-?\bnan\b")

ARCH_NAMES = ["n150", "n300", "p150"]


def arch_label_from_artifact(name: str) -> str | None:
    """Return the arch's card name (e.g. "n150") or None if unrecognized."""
    for card in ARCH_NAMES:
        if card in name:
            return card
    return None


# --- Data collection -------------------------------------------------------------


def read_arch_results(json_dir: Path, arch_label: str = "(unknown)") -> dict:
    """Read every `<title>.json` file directly inside `json_dir` and return
    per-test/per-case throughputs for a single arch.

    Returns: { test_title: { case_name: { "throughput": float, "unit": str } } }

    `arch_label` is only used for diagnostic warnings. This is the per-arch
    building block used both by the CI walker (`collect_current_results`) and
    by `compare_to_baseline.py` for local runs.
    """
    per_arch: dict = defaultdict(dict)
    for path in sorted(json_dir.glob("*.json")):
        title = path.stem
        if title == "machine_host_spec":
            continue
        try:
            text = _NAN_RE.sub("NaN", path.read_text())
            data = json.loads(text)
        except (json.JSONDecodeError, OSError) as e:
            print(
                f"WARN: skipping {arch_label}/{title}: cannot read JSON ({e})",
                file=sys.stderr,
            )
            continue
        for r in data.get("results", []):
            case = r.get("name")
            med = r.get("median(elapsed)")
            batch = r.get("batch")
            unit = r.get("unit") or "byte"
            if case is None or med is None or batch is None:
                continue
            if med == 0 or batch == 0:
                print(
                    f"WARN: skipping {arch_label}/{title}/{case}: med={med} batch={batch}",
                    file=sys.stderr,
                )
                continue
            if not (math.isfinite(med) and math.isfinite(batch)):
                print(
                    f"WARN: skipping {arch_label}/{title}/{case}: "
                    f"non-finite med={med} batch={batch}",
                    file=sys.stderr,
                )
                continue
            per_arch[title][case] = {
                "throughput": batch / med,
                "unit": unit,
            }
    return per_arch


def collect_current_results(current_dir: Path) -> dict:
    """Walk `current_dir` looking for `benchmark-json-*` subdirs (one per
    arch) and harvest per-case throughputs.

    Returns: { arch_label: { test_title: { case_name: { "throughput": float, "unit": str } } } }

    The C++ exporter writes flat `<artifact>/<title>.json` files (see
    `microbenchmark_utils.hpp::export_results`).
    """
    results: dict = defaultdict(lambda: defaultdict(dict))
    for artifact_dir in sorted(current_dir.iterdir()):
        if not artifact_dir.is_dir():
            continue
        if not artifact_dir.name.startswith("benchmark-json-"):
            continue
        arch = arch_label_from_artifact(artifact_dir.name)
        if arch is None:
            print(
                f"WARN: cannot derive arch label from artifact {artifact_dir.name}; skipping",
                file=sys.stderr,
            )
            continue
        results[arch] = read_arch_results(artifact_dir, arch)
    return results


def load_baselines_dir(baselines_dir: Path) -> dict:
    """Load every `<arch>.yaml` file in `baselines_dir` and combine into the
    nested shape that `render_summary` consumes:

        { test_title: { case_name: { arch_label: entry } } }

    Plus a top-level `metadata` dict that records per-arch calibration info:

        { "metadata": { "archs": { "<arch label>": {
              "runner_hostname": ...,
              "calibrated_at": ...,
          } } } }

    The arch label is taken from each file's `metadata.arch` field so the
    filename is not load-bearing.
    """
    combined: dict = {"metadata": {"archs": {}}}
    for yaml_path in sorted(baselines_dir.glob("*.yaml")):
        with open(yaml_path) as f:
            data = yaml.safe_load(f) or {}
        per_arch_meta = data.get("metadata") or {}
        arch = per_arch_meta.get("arch")
        if not arch:
            print(
                f"WARN: {yaml_path} has no metadata.arch field; skipping",
                file=sys.stderr,
            )
            continue
        combined["metadata"]["archs"][arch] = {
            "runner_hostname": per_arch_meta.get("runner_hostname"),
            "calibrated_at": per_arch_meta.get("calibrated_at"),
        }
        for title, cases in data.items():
            if title == "metadata":
                continue
            if not isinstance(cases, dict):
                continue
            for case_name, entry in cases.items():
                combined.setdefault(title, {}).setdefault(case_name, {})[arch] = entry
    return combined


# --- Classification --------------------------------------------------------------


def classify(
    current_thr: float, baseline_thr: float, tolerance_pct: float
) -> tuple[str, float]:
    """Return (status, signed Δ%). Δ% = (current − baseline) / baseline × 100."""
    delta_pct = (current_thr - baseline_thr) / baseline_thr * 100.0
    if abs(delta_pct) <= tolerance_pct:
        return "OK", delta_pct
    if delta_pct > tolerance_pct:
        return "UP", delta_pct
    return "DOWN", delta_pct


# --- Rendering -------------------------------------------------------------------

# Hybrid emoji + ASCII labels. The Step Summary tab renders the emoji as a
# colored glyph; the raw job log and `grep` still match the ASCII suffix. OK
# stays plain — no need to call attention to passing cases.
STATUS_DECORATION = {
    "OK": "OK",
    "UP": "🟢 UP",
    "DOWN": "🔴 DOWN",
}


def decorate_status(status: str) -> str:
    return STATUS_DECORATION.get(status, status)


def format_cell(counts: dict, total: int) -> str:
    """Build the coarse-table cell string from per-status counts."""
    down, up = counts.get("DOWN", 0), counts.get("UP", 0)
    if down + up == 0:
        return f"OK ({total}/{total})"
    parts = []
    if down:
        parts.append(f"{down} {decorate_status('DOWN')}")
    if up:
        parts.append(f"{up} {decorate_status('UP')}")
    return ", ".join(parts) + f" (of {total})"


# Width of the truncation for detail subtables — see plan §"Detail section rules".
DETAIL_TRUNCATION_LIMIT = 10


def render_detail_subtable(title: str, rows: list) -> str:
    """Render a markdown table for one direction of breaches.

    `rows` is a list of (case_name, current_thr, baseline_thr, delta_pct,
    tolerance_pct, status, unit) tuples, presumed already sorted.
    """
    lines = [f"**{title} ({len(rows)})**", ""]
    lines.append("| Case | Current | Baseline | Δ% | Tolerance | Status |")
    lines.append("|------|--------:|---------:|---:|----------:|:-------|")
    shown = rows[:DETAIL_TRUNCATION_LIMIT]
    for case, cur, base, dpct, tol, status, unit in shown:
        # format_throughput hides the "byte" pseudo-unit; pass through other units
        unit_arg = None if unit == "byte" else unit
        lines.append(
            f"| {case} | {format_throughput(cur, unit_arg)} | "
            f"{format_throughput(base, unit_arg)} | "
            f"{dpct:+.2f}% | ±{tol:g}% | {decorate_status(status)} |"
        )
    if len(rows) > DETAIL_TRUNCATION_LIMIT:
        lines.append("")
        lines.append(
            f"… and {len(rows) - DETAIL_TRUNCATION_LIMIT} more cases beyond the threshold."
        )
    return "\n".join(lines)


def render_detail_section(
    test_title: str,
    arch: str,
    breached: list,
    stable: list,
) -> str:
    """Render the full detail block for one breaching (test, arch) cell."""
    n_breach = len(breached)
    n_total = n_breach + len(stable)
    lines = [
        "",
        f"### {test_title} on {arch} — {n_breach} of {n_total} cases outside tolerance",
        "",
    ]
    slowdowns = [r for r in breached if r[3] < 0]
    speedups = [r for r in breached if r[3] > 0]
    slowdowns.sort(key=lambda r: r[3])  # most negative first
    speedups.sort(key=lambda r: -r[3])  # most positive first
    if slowdowns:
        lines.append(render_detail_subtable("Slowdowns", slowdowns))
        lines.append("")
    if speedups:
        lines.append(render_detail_subtable("Speedups", speedups))
        lines.append("")
    # Footer: list stable cases if there are few; otherwise summarize the count.
    if not stable:
        pass
    elif len(stable) <= 3:
        names = ", ".join(
            f"{name} (Δ = {dpct:+.2f}%, within ±{tol:g}%)"
            for name, _cur, _base, dpct, tol, _st, _unit in stable
        )
        lines.append(f"_Stable cases not shown: {names}._")
    else:
        # Tolerances can vary widely within a single suite (e.g. 5% on a clean
        # 1 MiB transfer vs 40% on a noisy 1-byte one), so quoting a single
        # number would misrepresent most cases. Show the span instead.
        tols = [r[4] for r in stable]
        tol_min, tol_max = min(tols), max(tols)
        span = (
            f"±{tol_min:g}%"
            if tol_min == tol_max
            else f"±{tol_min:g}% to ±{tol_max:g}%"
        )
        lines.append(
            f"_Stable: {len(stable)} of {n_total} cases within tolerance ({span} per-case)._"
        )
    return "\n".join(lines)


def render_summary(current: dict, baselines: dict) -> tuple[str, list, list]:
    """Top-level renderer. Returns (markdown document, gated_breaches, missing_gated).

    `gated_breaches` is a list of (title, case, arch, status, dpct, tol) for
    cases that breached tolerance AND carry `gate: true` in the per-arch baselines —
    the caller exits non-zero when this list is non-empty.

    `missing_gated` is a list of (title, case, arch) for gated cases present
    in the per-arch baselines but absent from this run's JSON (whole-arch missing or
    per-case missing). Surfaced as a warning so a partial benchmark run can't
    be confused with a clean one; does not fail the job.
    """
    meta = baselines.get("metadata") or {}
    archs_meta: dict = meta.get("archs") or {}

    # Tests come from baselines (so a missing-from-baseline test is visible but
    # never alerts).
    test_titles = [k for k in baselines if k != "metadata"]
    archs = set()
    for title in test_titles:
        for case_entry in baselines[title].values():
            archs.update(case_entry.keys())
    arch_order = sorted(archs)

    # Per-(test, arch): collect (counts, breached_rows, stable_rows).
    cell_state: dict = {}
    gated_breaches: list = []
    missing_gated: list = []
    for title in test_titles:
        for arch in arch_order:
            counts = {"OK": 0, "UP": 0, "DOWN": 0}
            breached, stable = [], []

            arch_results = current.get(arch, {}).get(title, {})
            # Tests in the per-arch baselines define which cases we evaluate. A new case
            # in CI that's not in baselines is informational and skipped here
            # (added in next refresh). A baseline case missing from CI marks
            # the cell as "no result".
            cases_with_baseline = {
                case_name: case_entry[arch]
                for case_name, case_entry in baselines[title].items()
                if arch in case_entry
            }
            if not cases_with_baseline:
                cell_state[(title, arch)] = (counts, breached, stable, "no baseline")
                continue
            if not arch_results:
                for case_name, baseline_entry in cases_with_baseline.items():
                    if baseline_entry.get("gate") is True:
                        missing_gated.append((title, case_name, arch))
                cell_state[(title, arch)] = (counts, breached, stable, "no result")
                continue
            for case_name, baseline_entry in cases_with_baseline.items():
                if case_name not in arch_results:
                    if baseline_entry.get("gate") is True:
                        missing_gated.append((title, case_name, arch))
                    continue
                current_entry = arch_results[case_name]
                current_thr = current_entry["throughput"]
                unit = current_entry["unit"]
                baseline_thr = baseline_entry["median_value"]
                tolerance_pct = baseline_entry["tolerance_pct"]
                status, dpct = classify(current_thr, baseline_thr, tolerance_pct)
                row = (
                    case_name,
                    current_thr,
                    baseline_thr,
                    dpct,
                    tolerance_pct,
                    status,
                    unit,
                )
                counts[status] += 1
                if status == "OK":
                    stable.append(row)
                else:
                    breached.append(row)
                    if baseline_entry.get("gate") is True and status == "DOWN":
                        gated_breaches.append(
                            (title, case_name, arch, status, dpct, tolerance_pct)
                        )
            tag = None if (counts["OK"] + len(breached)) > 0 else "no result"
            cell_state[(title, arch)] = (counts, breached, stable, tag)

    # Build coarse table.
    header_cols = ["Test"] + arch_order
    lines = [
        "## UMD perf regression check vs in-repo baseline",
        "",
    ]
    # Per-arch calibration line: each arch may have its own runner and refresh
    # date now that the baselines live in per-arch YAMLs.
    if archs_meta:
        per_arch_notes = []
        for arch in arch_order:
            m = archs_meta.get(arch) or {}
            per_arch_notes.append(
                f"**{arch}**: `{m.get('runner_hostname', '?')}` "
                f"({m.get('calibrated_at', '?')})"
            )
        lines.append("_Baselines: " + "; ".join(per_arch_notes) + "._")
    else:
        lines.append("_Baselines: (no metadata found)._")
    lines.extend(
        [
            "",
            "| " + " | ".join(header_cols) + " |",
            "|" + "|".join(["---"] * len(header_cols)) + "|",
        ]
    )
    for title in test_titles:
        row = [title]
        for arch in arch_order:
            counts, breached, stable, tag = cell_state[(title, arch)]
            if tag in {"no baseline", "no result"}:
                row.append(f"— ({tag})")
            else:
                total = sum(counts.values())
                row.append(format_cell(counts, total))
        lines.append("| " + " | ".join(row) + " |")

    # Detail sections, in the same order as the coarse table.
    detail_chunks = []
    for title in test_titles:
        for arch in arch_order:
            counts, breached, stable, tag = cell_state[(title, arch)]
            if breached:
                detail_chunks.append(
                    render_detail_section(title, arch, breached, stable)
                )

    if detail_chunks:
        lines.append("")
        lines.append("---")
        lines.extend(detail_chunks)

    if gated_breaches:
        lines.append("")
        lines.append("---")
        lines.append("")
        lines.append(f"### Gated breaches — failing the job ({len(gated_breaches)})")
        lines.append("")
        lines.append("| Test | Case | Arch | Status | Δ% | Tolerance |")
        lines.append("|------|------|------|:-------|---:|----------:|")
        for title, case, arch, status, dpct, tol in gated_breaches:
            lines.append(
                f"| {title} | {case} | {arch} | {decorate_status(status)} | "
                f"{dpct:+.2f}% | ±{tol:g}% |"
            )

    if missing_gated:
        lines.append("")
        lines.append("---")
        lines.append("")
        lines.append(
            f"### Missing gated cases — not failing the job ({len(missing_gated)})"
        )
        lines.append("")
        lines.append(
            "_Cases below carry `gate: true` in the per-arch baselines but produced no "
            "result in this run. A partial benchmark run (e.g. binary crashed "
            "mid-suite) will surface here; an intentional case rename/removal "
            "will too, until the next baseline refresh._"
        )
        lines.append("")
        lines.append("| Test | Case | Arch |")
        lines.append("|------|------|------|")
        for title, case, arch in missing_gated:
            lines.append(f"| {title} | {case} | {arch} |")

    return "\n".join(lines) + "\n", gated_breaches, missing_gated


# --- Main ------------------------------------------------------------------------


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--current",
        type=Path,
        required=True,
        help="Directory containing per-arch `benchmark-json-*` subdirs (this CI run).",
    )
    p.add_argument(
        "--baselines-dir",
        type=Path,
        required=True,
        help="Directory of per-arch baseline YAMLs (one file per arch).",
    )
    p.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output markdown file path.",
    )
    args = p.parse_args()

    try:
        current_path = args.current.resolve(strict=True)
        baselines_dir = args.baselines_dir.resolve(strict=True)
    except FileNotFoundError as e:
        sys.exit(f"input path does not exist: {e.filename}")
    output_path = args.output.resolve()

    if not current_path.is_dir():
        sys.exit(f"--current is not a directory: {current_path}")
    if not baselines_dir.is_dir():
        sys.exit(f"--baselines-dir is not a directory: {baselines_dir}")

    baselines = load_baselines_dir(baselines_dir)
    if not baselines.get("metadata", {}).get("archs"):
        sys.exit(f"--baselines-dir contains no usable per-arch YAMLs: {baselines_dir}")

    current = collect_current_results(current_path)

    if not current:
        print(
            f"FAIL: no benchmark results collected from {current_path}.",
            file=sys.stderr,
        )
        return 1

    summary, gated_breaches, missing_gated = render_summary(current, baselines)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(summary)
    if missing_gated:
        print(
            f"WARN: {len(missing_gated)} gated case(s) missing from this run "
            f"(see Missing gated cases section).",
            file=sys.stderr,
        )
        for title, case, arch in missing_gated:
            print(f"  - {title} :: {case} :: {arch}", file=sys.stderr)
    # Soft alerts (non-gated breaches) never fail; cases with `gate: true` in
    # the per-arch baselines fail the job when they breach DOWN.
    if gated_breaches:
        print(
            f"FAIL: {len(gated_breaches)} gated case(s) breached tolerance.",
            file=sys.stderr,
        )
        for title, case, arch, status, dpct, tol in gated_breaches:
            print(
                f"  - {title} :: {case} :: {arch}: {decorate_status(status)} "
                f"{dpct:+.2f}% (tol ±{tol:g}%)",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
