# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Refresh `baselines.yaml` from the last N successful main runs of the
benchmarks workflow.

Per (test, case, arch) tuple, collects throughput samples across runs,
computes the median and the median absolute deviation (MAD), and writes:
    median_throughput = median(samples)
    tolerance_pct     = max(5, ceil(4.5 * mad_pct))

MAD is the median of |sample - median|. It is robust to single-run
outliers — one noisy CI run shifts MAD by at most one rank, whereas it
would inflate stdev disproportionately because stdev squares deviations.
The K=4.5 factor gives ~99.7% coverage of a normal distribution. 
The 5% floor stops near-deterministic tests from getting a 0% tolerance
 that would alert on rounding noise; ceil keeps tolerance slightly wider
   rather than tighter.

Existing `gate: true` flags in the YAML are preserved across refreshes —
the refresh tool should never silently un-gate a case.

Usage:
    GH_TOKEN=ghp_... python3 refresh_baselines.py \\
        --workflow build-and-run-all-benchmarks.yml \\
        --runs 30 \\
        --output tests/microbenchmark/expected/baselines.yaml

After the script writes the YAML, review the diff and submit it as a PR.
"""

import argparse
import io
import json
import math
import os
import statistics
import sys
import urllib.error
import urllib.request
import zipfile
from collections import defaultdict
from datetime import date
from pathlib import Path

import yaml

# Reuse the canonical arch label mapping from summarize_regressions.py so the
# arch keys in baselines.yaml stay consistent across both tools.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from summarize_regressions import arch_label_from_artifact  # noqa: E402

REPO_DEFAULT = "tenstorrent/tt-umd"
MIN_SAMPLES_FOR_RELIABLE = 10
TOLERANCE_FLOOR_PCT = 5
# Multiplier on MAD-as-percent for the tolerance band. 4.5 gives ~99.7%
# coverage of a normal distribution.
MAD_K = 4.5


# --- GitHub API access -----------------------------------------------------------


class _NoAuthRedirect(urllib.request.HTTPRedirectHandler):
    """Don't forward Authorization header to the artifact's presigned blob URL."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return urllib.request.Request(newurl, method=req.get_method())


def _opener():
    return urllib.request.build_opener(_NoAuthRedirect())


def _gh_get(url: str, token: str, raw: bool = False):
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
        },
    )
    with _opener().open(req) as r:
        return r.read() if raw else json.load(r)


def list_recent_main_runs(repo: str, workflow: str, n: int, token: str) -> list[dict]:
    url = (
        f"https://api.github.com/repos/{repo}/actions/workflows/{workflow}/runs"
        f"?branch=main&status=success&per_page={n}"
    )
    return _gh_get(url, token)["workflow_runs"][:n]


def list_artifacts(repo: str, run_id: int, token: str) -> list[dict]:
    url = f"https://api.github.com/repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100"
    return _gh_get(url, token)["artifacts"]


def download_artifact(repo: str, artifact_id: int, token: str) -> bytes:
    return _gh_get(
        f"https://api.github.com/repos/{repo}/actions/artifacts/{artifact_id}/zip",
        token,
        raw=True,
    )


# --- Sample collection -----------------------------------------------------------


def collect_samples(repo: str, runs: list[dict], token: str) -> dict:
    """Walk each run's benchmark-json-* artifacts, return:
    { arch_label: { test_title: { case_name: [throughput, throughput, ...] } } }
    """
    samples: dict = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    units: dict = defaultdict(
        lambda: defaultdict(dict)
    )  # arch -> title -> case -> unit
    for r in runs:
        sha = r["head_sha"][:8]
        print(f"  run {r['id']} sha={sha}", file=sys.stderr)
        try:
            arts = list_artifacts(repo, r["id"], token)
        except urllib.error.HTTPError as e:
            print(f"    skip: {e}", file=sys.stderr)
            continue
        bench_arts = [a for a in arts if a["name"].startswith("benchmark-json-")]
        for a in bench_arts:
            arch = arch_label_from_artifact(a["name"])
            if arch is None:
                continue
            try:
                zb = download_artifact(repo, a["id"], token)
            except urllib.error.HTTPError as e:
                print(f"    artifact {a['name']}: {e}", file=sys.stderr)
                continue
            z = zipfile.ZipFile(io.BytesIO(zb))
            # Latest-wins per title (handles timestamped subdirs).
            by_title: dict[str, str] = {}
            for n in sorted(z.namelist()):
                if not n.endswith(".json"):
                    continue
                title = n.split("/")[-1][: -len(".json")]
                if title == "machine_host_spec":
                    continue
                by_title[title] = n
            for title, path in by_title.items():
                with z.open(path) as f:
                    data = json.load(f)
                for result in data.get("results", []):
                    case = result.get("name")
                    med = result.get("median(elapsed)")
                    batch = result.get("batch")
                    unit = result.get("unit") or "byte"
                    if case is None or med is None or batch is None:
                        continue
                    if med == 0 or batch == 0:
                        print(
                            f"WARN: skipping {arch}/{title}/{case}: med={med} batch={batch}",
                            file=sys.stderr,
                        )
                        continue
                    samples[arch][title][case].append(batch / med)
                    units[arch][title][case] = unit
    return samples, units


# --- Tolerance derivation --------------------------------------------------------


def derive_baseline_entry(samples: list[float]) -> tuple[float, float, str]:
    """Return (median_throughput, tolerance_pct, comment).

    `comment` is a free-text annotation (may be empty) for the YAML.
    """
    if len(samples) < MIN_SAMPLES_FOR_RELIABLE:
        suffix = f"  # only {len(samples)} samples — tolerance may be too tight/loose"
    else:
        suffix = ""
    median_throughput = statistics.median(samples)
    if len(samples) < 2:
        # No spread to measure with one sample — use the floor.
        return median_throughput, float(TOLERANCE_FLOOR_PCT), suffix
    # Robust spread: median of absolute deviations from the median, expressed
    # as a percentage of the median so the tolerance is unit-agnostic.
    mad = statistics.median(abs(s - median_throughput) for s in samples)
    mad_pct = mad / median_throughput * 100 if median_throughput else 0.0
    tolerance_pct = max(TOLERANCE_FLOOR_PCT, math.ceil(MAD_K * mad_pct))
    return median_throughput, float(tolerance_pct), suffix


# --- YAML rendering --------------------------------------------------------------


def render_yaml(
    samples: dict,
    existing_gates: dict,
    runs_used: int,
    workflow: str,
) -> tuple[str, list, list]:
    """Render baselines.yaml content. Returns (text, diff_lines, low_sample_lines)."""
    diff_lines: list[str] = []
    low_sample_lines: list[str] = []
    out = io.StringIO()

    out.write(f"# tests/microbenchmark/expected/baselines.yaml\n")
    out.write(f"# Calibrated from {runs_used} successful main runs of {workflow}.\n")
    out.write(
        "# To refresh: tests/microbenchmark/tools/refresh_baselines.py --runs 30\n"
    )
    out.write("#\n")
    out.write("# Schema:\n")
    out.write("#   <bench_title>:\n")
    out.write("#     <case_name>:\n")
    out.write("#       <arch_label>:\n")
    out.write(
        "#         median_throughput: float       # ops/s (batch / median_elapsed)\n"
    )
    out.write(
        "#         tolerance_pct: float           # symmetric +-%; alert if |delta%| > this\n"
    )
    out.write(
        "#         gate: bool (optional)          # Phase 2 hard gates; preserved on refresh\n"
    )
    out.write("\n")
    out.write("metadata:\n")
    out.write(f'  calibrated_at: "{date.today().isoformat()}"\n')
    out.write(f"  calibrated_from_runs: {runs_used}\n")
    out.write('  notes: "Pilot — soft alerts only."\n')
    out.write("\n")

    # Deterministic ordering: tests alphabetical, archs alphabetical within case.
    all_titles = sorted({title for arch in samples for title in samples[arch]})
    for title in all_titles:
        out.write(f"{title}:\n")
        # cases that appear in any arch for this title
        all_cases: set = set()
        for arch in samples:
            all_cases.update(samples[arch].get(title, {}).keys())
        for case in sorted(all_cases):
            out.write(f"  {_yaml_escape(case)}:\n")
            archs_for_case = sorted(
                arch for arch in samples if case in samples[arch].get(title, {})
            )
            for arch in archs_for_case:
                series = samples[arch][title][case]
                median_thr, tolerance_pct, suffix = derive_baseline_entry(series)
                gate = existing_gates.get((title, case, arch), False)
                gate_str = ", gate: true" if gate else ""
                # Track tolerance shift vs existing YAML for review.
                old_tolerance = existing_gates.get(("__tolerance__", title, case, arch))
                if old_tolerance is not None and abs(tolerance_pct - old_tolerance) > 5:
                    diff_lines.append(
                        f"  {title} :: {case} :: {arch}: "
                        f"tolerance {old_tolerance:g}% -> {tolerance_pct:g}%"
                    )
                if len(series) < MIN_SAMPLES_FOR_RELIABLE:
                    low_sample_lines.append(
                        f"  {title} :: {case} :: {arch}: {len(series)} sample(s)"
                    )
                out.write(
                    f"    {_yaml_escape(arch)}: "
                    f"{{ median_throughput: {median_thr:.4g}, "
                    f"tolerance_pct: {tolerance_pct:g}{gate_str} }}{suffix}\n"
                )
        out.write("\n")

    return out.getvalue(), diff_lines, low_sample_lines


def _yaml_escape(s: str) -> str:
    """Wrap a string in quotes if it contains characters that confuse YAML."""
    if any(c in s for c in ":#,&*!|>'\"%@`{}[]\n") or s.strip() != s:
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


def load_existing_gates(path: Path) -> dict:
    """Read existing YAML to preserve `gate: true` flags and capture old tolerances."""
    state: dict = {}
    if not path.exists():
        return state
    with open(path) as f:
        data = yaml.safe_load(f) or {}
    for title, cases in data.items():
        if title == "metadata":
            continue
        if not isinstance(cases, dict):
            continue
        for case_name, arch_entries in cases.items():
            if not isinstance(arch_entries, dict):
                continue
            for arch, entry in arch_entries.items():
                if not isinstance(entry, dict):
                    continue
                if entry.get("gate") is True:
                    state[(title, case_name, arch)] = True
                tol = entry.get("tolerance_pct")
                if tol is not None:
                    state[("__tolerance__", title, case_name, arch)] = float(tol)
    return state


# --- Main ------------------------------------------------------------------------


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--repo", default=REPO_DEFAULT, help="owner/repo, default tenstorrent/tt-umd"
    )
    p.add_argument(
        "--workflow",
        default="build-and-run-all-benchmarks.yml",
        help="Workflow file name (basename, .yml extension).",
    )
    p.add_argument("--runs", type=int, default=30, help="Number of main runs to use.")
    p.add_argument(
        "--output",
        type=Path,
        default=Path("tests/microbenchmark/expected/baselines.yaml"),
        help="Where to write the refreshed YAML.",
    )
    args = p.parse_args()

    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if not token:
        sys.exit("Set GH_TOKEN (or GITHUB_TOKEN) in the environment.")

    print(
        f"Fetching last {args.runs} successful main runs of {args.workflow}...",
        file=sys.stderr,
    )
    runs = list_recent_main_runs(args.repo, args.workflow, args.runs, token)
    if not runs:
        sys.exit("No successful runs found.")
    print(f"Got {len(runs)} runs. Downloading benchmark artifacts...", file=sys.stderr)
    samples, _units = collect_samples(args.repo, runs, token)
    if not samples:
        sys.exit(
            "No samples collected — workflow may not produce benchmark-json-* artifacts."
        )

    output_path = args.output.resolve()

    existing_gates = load_existing_gates(output_path)
    yaml_text, diff_lines, low_sample_lines = render_yaml(
        samples, existing_gates, len(runs), args.workflow
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(yaml_text)
    print(f"Wrote {output_path}", file=sys.stderr)

    if diff_lines:
        print(
            "\nTolerances that shifted by more than 5 percentage points "
            "(review before committing):",
            file=sys.stderr,
        )
        for line in diff_lines:
            print(line, file=sys.stderr)
    else:
        print(
            "\n(No tolerance shifts > 5 percentage points vs existing YAML.)",
            file=sys.stderr,
        )

    if low_sample_lines:
        print(
            f"\n{len(low_sample_lines)} case(s) calibrated from fewer than "
            f"{MIN_SAMPLES_FOR_RELIABLE} samples (review before committing):",
            file=sys.stderr,
        )
        for line in low_sample_lines:
            print(line, file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
