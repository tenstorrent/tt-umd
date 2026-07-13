# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Convert CI nanobench results into the Tenstorrent perf-data JSON schema.

This is the bridge between the microbenchmark pipeline and Superset. It reads
the same `benchmark-json-*` artifacts that `regression-check.yml` consumes
(one subdirectory per arch, each holding `<title>.json` nanobench outputs plus
a `machine_host_spec.json`) and emits one `benchmark_<pipeline>_<arch>_<ts>.json`
file per arch in the shape the Data team's `benchmark` pipeline ingests:
`sw_test.benchmark_run` + `sw_test.benchmark_measurement`.

The output matches the `CompleteBenchmarkRun` / `BenchmarkMeasurement` pydantic
models used by `tenstorrent/tt-github-actions`'s `collect_data` action
(schema mirror kept in sync by the field checks in `validate_run` below):
https://github.com/tenstorrent/tt-github-actions/blob/main/.github/actions/collect_data/src/pydantic_models.py

Run-level mapping (per arch, per CI run):

    run_type                = --run-type              (default "umd_microbenchmark")
    ml_model_name           = --run-type              (NOT NULL in the DB; reused as the suite family)
    device_hostname         = host_info.CI_Runner     (falls back to Hostname, then arch)
    device_info             = the whole host spec      (BoardType, PCIe lanes, governor, …)
    git_* / github_*        = CI context (args/env)

Per case, nanobench gives one aggregate result. `benchmark_measurement` holds a
single numeric `value` per row, so each metric of a case becomes its own row,
distinguished by a ` | <metric>` suffix on `name` (same idea as the reference
fixture's `total_samples` / `total_time` rows). For each nanobench case we emit:

    step_name = nanobench <title>                     (the "test": TLB_DRAM, PCIe_DMA, …)
    name      = "<case> | throughput"        value =   batch / median(elapsed)
    name      = "<case> | median_elapsed_s"  value =   median(elapsed)
    name      = "<case> | total_time_s"      value =   totalTime  (Σ iterations × elapsed)
    name      = "<case> | err_pct"           value =   medianAbsolutePercentError(elapsed) × 100
    name      = "<case> | epochs"            value =   epochs run for this case (count)
    name      = "<case> | iterations"        value =   total iterations across all epochs (count)

`iteration` (the DB column) is always 1 — a placeholder for the NOT NULL field; the epoch
and iteration *counts* are exported as the `epochs` / `iterations` metric rows above, not
as per-sample rows. `target` is null (baselines not consumed).
Split `name` on " | " in the Superset dataset to get (case, metric); see tools/SUPERSET.md.

Example (local dry-run against a downloaded artifact tree):

    python3 tests/microbenchmark/tools/emit_benchmark_report.py \\
        --current /tmp/all-arch-results \\
        --output-dir /tmp/superset-json \\
        --dry-run
"""

import argparse
import json
import math
import os
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

from utils import load_nanobench_json

# Constant identifying every UMD microbenchmark row in the shared benchmark
# tables. Filter on this (git_repo_name='tt-umd' AND run_type='umd_microbenchmark')
# in Superset to isolate UMD data from the other teams sharing the table.
DEFAULT_RUN_TYPE = "umd_microbenchmark"
DEFAULT_REPO_NAME = "tt-umd"

HOST_SPEC_FILENAME = "machine_host_spec.json"
METRIC_SEP = " | "


def _iso_utc(dt: datetime) -> str:
    """Render a tz-aware datetime as the `...Z` form the pipeline expects."""
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _now_utc_str() -> str:
    return _iso_utc(datetime.now(timezone.utc))


def _finite(x):
    """Return float(x) if x is a finite number, else None."""
    return float(x) if isinstance(x, (int, float)) and math.isfinite(x) else None


def read_host_spec(artifact_dir: Path) -> dict:
    """Return the parsed `machine_host_spec.json` for one arch artifact, or {}."""
    spec_path = artifact_dir / HOST_SPEC_FILENAME
    if not spec_path.exists():
        return {}
    try:
        return json.loads(spec_path.read_text())
    except (json.JSONDecodeError, OSError) as e:
        print(f"WARN: could not read {spec_path}: {e}", file=sys.stderr)
        return {}


def read_arch_metrics(artifact_dir: Path) -> dict:
    """Read every `<title>.json` nanobench output in one arch artifact.

    Returns { title: { case: { metric: value } } } where metric is one of
    throughput / median_elapsed_s / total_time_s / err_pct.
    """
    per_title: dict = defaultdict(dict)
    for path in sorted(artifact_dir.glob("*.json")):
        title = path.stem
        if path.name == HOST_SPEC_FILENAME:
            continue
        try:
            data = load_nanobench_json(path)
        except (json.JSONDecodeError, OSError) as e:
            print(f"WARN: skipping {artifact_dir.name}/{title}: {e}", file=sys.stderr)
            continue
        for r in data.get("results", []):
            case = r.get("name")
            med = _finite(r.get("median(elapsed)"))
            batch = _finite(r.get("batch"))
            if case is None or med is None or batch is None or med == 0 or batch == 0:
                continue
            metrics = {
                "throughput": batch / med,
                "median_elapsed_s": med,
            }
            total = _finite(r.get("totalTime"))
            if total is not None:
                metrics["total_time_s"] = total
            mape = _finite(r.get("medianAbsolutePercentError(elapsed)"))
            if mape is not None:
                metrics["err_pct"] = mape * 100.0
            # Counts (single numbers) of how much sampling backed the medians:
            # number of epochs, and total iterations summed across epochs.
            epochs = _finite(r.get("epochs"))
            if epochs is not None:
                metrics["epochs"] = epochs
            samples = r.get("measurements")
            if isinstance(samples, list) and samples:
                iters = [_finite(s.get("iterations")) for s in samples]
                iters = [i for i in iters if i is not None]
                if iters:
                    metrics["iterations"] = float(sum(iters))
            per_title[title][case] = metrics
    return per_title


def build_run(arch: str, host_spec: dict, per_title: dict, ci: dict) -> dict | None:
    """Assemble one CompleteBenchmarkRun dict for a single arch's artifact.

    Returns None (with a warning) when the artifact yields no usable cases.
    """
    if not per_title:
        print(
            f"WARN: arch {arch}: no usable nanobench results; skipping.",
            file=sys.stderr,
        )
        return None

    host_info = host_spec.get("host_info") or {}
    device_hostname = host_info.get("CI_Runner") or host_info.get("Hostname") or arch

    # device_info is JSONB in the DB — stash the full host spec so BoardType,
    # PCIe lane config, CPU governor, driver version, etc. are all queryable.
    device_info = {k: v for k, v in host_info.items() if v is not None}
    device_info["BoardType"] = arch
    if host_spec.get("tt_pcie_lane_info"):
        device_info["tt_pcie_lane_info"] = host_spec["tt_pcie_lane_info"]

    run_start_ts = ci["run_start_ts"]
    run_end_ts = ci["run_end_ts"]

    measurements = []
    for title, cases in per_title.items():
        for case, metrics in cases.items():
            for metric, value in metrics.items():
                if _finite(value) is None:
                    continue
                measurements.append(
                    {
                        "step_start_ts": run_start_ts,
                        "step_end_ts": run_end_ts,
                        "iteration": 1,
                        "step_name": title,
                        "step_warm_up_num_iterations": None,
                        "name": f"{case}{METRIC_SEP}{metric}",
                        "value": float(value),
                        # Baselines are intentionally not consumed yet — charts show
                        # plain current results. Re-enable via baselines/ later.
                        "target": None,
                        "device_power": None,
                        "device_temperature": None,
                    }
                )

    if not measurements:
        print(f"WARN: arch {arch}: no finite measurements; skipping.", file=sys.stderr)
        return None

    return {
        "run_start_ts": run_start_ts,
        "run_end_ts": run_end_ts,
        "run_type": ci["run_type"],
        "git_repo_name": ci["git_repo_name"],
        "git_commit_hash": ci["git_commit_hash"],
        "git_commit_ts": ci["git_commit_ts"],
        "git_branch_name": ci["git_branch_name"],
        "github_pipeline_id": ci["github_pipeline_id"],
        "github_pipeline_link": ci["github_pipeline_link"],
        "github_job_id": ci["github_job_id"],
        "user_name": ci["git_author"],
        "docker_image": ci["docker_image"],
        "device_hostname": device_hostname,
        "device_ip": None,
        "device_info": device_info,
        # ml_model_name is NOT NULL in the shared table; UMD has no model, so we
        # reuse the run_type as the suite-family label. See tools/SUPERSET.md.
        "ml_model_name": ci["run_type"],
        "ml_model_type": None,
        "num_layers": None,
        "batch_size": None,
        "config_params": None,
        "precision": None,
        "dataset_name": None,
        "profiler_name": "nanobench",
        "input_sequence_length": None,
        "output_sequence_length": None,
        "image_dimension": None,
        "perf_analysis": False,
        "training": False,
        "measurements": measurements,
    }


# Fields the DB rejects when NULL/empty (NOT NULL columns) — fail loudly in CI
# rather than get the whole file quarantined by the Data team's wrangler.
_REQUIRED_RUN_FIELDS = (
    "run_start_ts",
    "run_end_ts",
    "run_type",
    "device_hostname",
    "ml_model_name",
)
_REQUIRED_MEASUREMENT_FIELDS = (
    "step_start_ts",
    "step_end_ts",
    "iteration",
    "step_name",
    "name",
    "value",
)


def validate_run(run: dict) -> list[str]:
    """Return a list of schema problems (empty list == valid)."""
    problems = []
    for field in _REQUIRED_RUN_FIELDS:
        if run.get(field) in (None, ""):
            problems.append(f"run.{field} is required but missing/empty")
    if not run.get("measurements"):
        problems.append("run has no measurements")
    for i, m in enumerate(run.get("measurements", [])):
        for field in _REQUIRED_MEASUREMENT_FIELDS:
            if m.get(field) in (None, ""):
                problems.append(
                    f"measurements[{i}].{field} is required but missing/empty"
                )
        if _finite(m.get("value")) is None:
            problems.append(
                f"measurements[{i}].value is not a finite number: {m.get('value')!r}"
            )
    return problems


def _clean(value):
    """Empty/whitespace CLI args (e.g. an unset ${{ github.* }}) → None.

    Guards against emitting `"git_commit_ts": ""`, which fails the datetime
    validation in the Data team's wrangler.
    """
    if value is None:
        return None
    value = str(value).strip()
    return value or None


def ci_metadata(args) -> dict:
    """Resolve run-level CI metadata from args, falling back to GH Action env."""
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    run_id = _clean(args.github_pipeline_id) or os.environ.get("GITHUB_RUN_ID")
    link = _clean(args.github_pipeline_link)
    if not link and repo and run_id:
        link = f"{server}/{repo}/actions/runs/{run_id}"
    job_id = _clean(args.github_job_id)
    return {
        "run_type": args.run_type,
        "git_repo_name": _clean(args.git_repo_name)
        or (repo.split("/")[-1] if repo else DEFAULT_REPO_NAME),
        "git_commit_hash": _clean(args.git_commit_hash) or os.environ.get("GITHUB_SHA"),
        "git_commit_ts": _clean(args.git_commit_ts),
        "git_branch_name": _clean(args.git_branch_name)
        or os.environ.get("GITHUB_REF_NAME"),
        "github_pipeline_id": int(run_id) if run_id else None,
        "github_pipeline_link": link,
        "github_job_id": int(job_id) if job_id else None,
        "git_author": _clean(args.git_author) or os.environ.get("GITHUB_ACTOR"),
        "docker_image": _clean(args.docker_image),
        "run_start_ts": _clean(args.run_start_ts) or _now_utc_str(),
        "run_end_ts": _clean(args.run_end_ts) or _now_utc_str(),
    }


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument(
        "--current",
        type=Path,
        required=True,
        help="Directory containing per-arch `benchmark-json-*` subdirs (this CI run).",
    )
    p.add_argument(
        "--output-dir",
        type=Path,
        default=Path("."),
        help="Where to write benchmark_<pipeline>_<arch>_<ts>.json files (default: cwd).",
    )
    p.add_argument("--run-type", default=DEFAULT_RUN_TYPE)
    p.add_argument("--git-repo-name", dest="git_repo_name", default=None)
    p.add_argument("--git-commit-hash", dest="git_commit_hash", default=None)
    p.add_argument("--git-commit-ts", dest="git_commit_ts", default=None)
    p.add_argument("--git-branch-name", dest="git_branch_name", default=None)
    p.add_argument("--git-author", dest="git_author", default=None)
    p.add_argument("--github-pipeline-id", dest="github_pipeline_id", default=None)
    p.add_argument("--github-pipeline-link", dest="github_pipeline_link", default=None)
    p.add_argument("--github-job-id", dest="github_job_id", default=None)
    p.add_argument("--docker-image", dest="docker_image", default=None)
    p.add_argument(
        "--run-start-ts",
        dest="run_start_ts",
        default=None,
        help="ISO8601 UTC; default now.",
    )
    p.add_argument(
        "--run-end-ts",
        dest="run_end_ts",
        default=None,
        help="ISO8601 UTC; default now.",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print JSON to stdout instead of writing files.",
    )
    args = p.parse_args()

    try:
        current = args.current.resolve(strict=True)
    except FileNotFoundError:
        sys.exit(f"--current does not exist: {args.current}")
    if not current.is_dir():
        sys.exit(f"--current is not a directory: {current}")

    ci = ci_metadata(args)

    artifact_dirs = [
        d
        for d in sorted(current.iterdir())
        if d.is_dir() and d.name.startswith("benchmark-json-")
    ]
    if not artifact_dirs:
        print(
            f"FAIL: no benchmark-json-* subdirs under {current}. Nothing to emit.",
            file=sys.stderr,
        )
        return 1

    if not args.dry_run:
        args.output_dir.mkdir(parents=True, exist_ok=True)

    written = 0
    for artifact_dir in artifact_dirs:
        host_spec = read_host_spec(artifact_dir)
        arch = (host_spec.get("host_info") or {}).get("BoardType")
        if not arch or arch == "unknown":
            print(
                f"WARN: no detected BoardType in {artifact_dir.name}/{HOST_SPEC_FILENAME}; skipping.",
                file=sys.stderr,
            )
            continue
        per_title = read_arch_metrics(artifact_dir)
        run = build_run(arch, host_spec, per_title, ci)
        if run is None:
            continue
        problems = validate_run(run)
        if problems:
            print(f"FAIL: schema validation for arch {arch}:", file=sys.stderr)
            for problem in problems:
                print(f"  - {problem}", file=sys.stderr)
            return 1

        # Filename only needs to start with `benchmark_` and end `.json` — the
        # wrangler parses nothing from it. Arch keeps per-arch files distinct.
        ts = ci["run_start_ts"].replace(":", "").replace("-", "")
        fname = f"benchmark_{ci['github_pipeline_id'] or 'local'}_{arch}_{ts}.json"
        payload = json.dumps(run, indent=2)
        if args.dry_run:
            print(f"----- {fname} ({len(run['measurements'])} measurements) -----")
            print(payload)
        else:
            (args.output_dir / fname).write_text(payload + "\n")
            print(
                f"Wrote {args.output_dir / fname} "
                f"({len(run['measurements'])} measurements, arch {arch}).",
                file=sys.stderr,
            )
        written += 1

    if written == 0:
        print("FAIL: emitted no benchmark files.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
