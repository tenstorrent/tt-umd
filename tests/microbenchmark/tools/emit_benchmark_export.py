# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Emit one Superset-bound export JSON per arch run.

Reads the nanobench ``<title>.json`` files and ``machine_host_spec.json`` from a
benchmark results directory and folds them into a single ``{run, results[]}``
document matching the schema the Data team ingests over SFTP.

The document is intentionally normalized: the run/context block appears once and
every per-case result links to it implicitly by living in the same file. The
DB-side primary keys (``id``) and the run<->result foreign key are assigned on
ingest, so they are not emitted here. ``host_spec_json`` is left null for now.

Standalone by design (only depends on ``utils.load_nanobench_json``) so it can be
sparse-checked-out in CI without pulling in pandas/psutil.
"""

import argparse
import json
import math
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from utils import load_nanobench_json  # noqa: E402

# Written by gather_host_specs.py; excluded when harvesting nanobench results.
HOST_SPEC_FILENAME = "machine_host_spec.json"


def _num(value):
    """Return a JSON-safe float, collapsing NaN/inf (nanobench's unset marker) to None."""
    if value is None:
        return None
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    return f if math.isfinite(f) else None


def derive_arch(card):
    """Silicon arch from the board/card label (n*/sim-wh -> wormhole_b0, p*/sim-bh -> blackhole)."""
    if not card:
        return None
    c = card.lower()
    if "wh" in c or c.startswith("n"):
        return "wormhole_b0"
    if "bh" in c or c.startswith("p"):
        return "blackhole"
    return None


def _git(repo_dir, *args):
    """Run a git command in repo_dir, returning stripped stdout or None on any failure."""
    try:
        out = subprocess.check_output(
            ["git", "-C", str(repo_dir), *args],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
        return out or None
    except Exception:
        return None


def build_run_context(host_spec, arch_override, repo_dir):
    """Assemble the run/context block from the host spec, CI env vars and git."""
    host_info = (host_spec or {}).get("host_info", {})
    card = host_info.get("BoardType") or None
    if card == "unknown":
        card = None
    arch = arch_override or derive_arch(card)

    commit_sha = os.environ.get("GITHUB_SHA") or _git(repo_dir, "rev-parse", "HEAD")
    branch_name = os.environ.get("GITHUB_REF_NAME") or _git(
        repo_dir, "rev-parse", "--abbrev-ref", "HEAD"
    )
    commit_timedate = _git(repo_dir, "show", "-s", "--format=%cI", commit_sha or "HEAD")

    pipeline_id = os.environ.get("GITHUB_RUN_ID")
    try:
        pipeline_id = int(pipeline_id) if pipeline_id else None
    except ValueError:
        pipeline_id = None

    run_ts = (
        os.environ.get("RUN_TS")
        or (host_spec or {}).get("time")
        or datetime.now().isoformat()
    )

    return {
        "run_ts": run_ts,
        "github_pipeline_id": pipeline_id,
        "commit_sha": commit_sha,
        "commit_timedate": commit_timedate,
        "branch_name": branch_name,
        "card": card,
        "arch": arch,
        # "host OS" — prefer the descriptive distro string, fall back to the kernel OS name.
        "os": host_info.get("Distro") or host_info.get("OS") or None,
        # CI runner name (not the ephemeral container hostname).
        "hostname": host_info.get("CI_Runner") or None,
        # Left null for now; the full host spec can be folded in later.
        "host_spec_json": None,
    }


def build_result(nb_result):
    """Map one nanobench result object to a per-case export record."""
    batch = _num(nb_result.get("batch"))
    median_elapsed = _num(nb_result.get("median(elapsed)"))
    mape = _num(nb_result.get("medianAbsolutePercentError(elapsed)"))

    iterations = None
    measurements = nb_result.get("measurements")
    if measurements:
        iterations = sum(int(m.get("iterations", 0)) for m in measurements)

    throughput = None
    if batch is not None and median_elapsed:
        throughput = batch / median_elapsed

    return {
        "test_title": nb_result.get("title"),
        "case_name": nb_result.get("name"),
        "unit": nb_result.get("unit"),
        "batch_size": batch,
        "epochs": nb_result.get("epochs"),
        "iterations": iterations,
        # nanobench's totalTime = sumProduct(iterations, elapsed) = total wall-clock time.
        "median_total_time_s": _num(nb_result.get("totalTime")),
        # median per-op elapsed time (seconds).
        "median_result": median_elapsed,
        # nanobench reports the error as a fraction; export it as a percentage.
        "error_pct": mape * 100 if mape is not None else None,
        "throughput": throughput,
        # Baseline/target throughput is not consumed here yet.
        "target": None,
    }


def collect_results(results_dir):
    """Load every nanobench <title>.json in results_dir (skipping the host spec)."""
    results = []
    for path in sorted(results_dir.glob("*.json")):
        if path.name == HOST_SPEC_FILENAME:
            continue
        try:
            data = load_nanobench_json(path)
        except (OSError, json.JSONDecodeError) as e:
            print(f"WARN: skipping {path.name}: {e}", file=sys.stderr)
            continue
        for nb_result in data.get("results", []):
            results.append(build_result(nb_result))
    return results


def main():
    parser = argparse.ArgumentParser(
        description="Fold nanobench results + host spec into a Superset export JSON."
    )
    parser.add_argument(
        "--results-dir",
        type=Path,
        required=True,
        help="Directory holding the nanobench <title>.json files and machine_host_spec.json.",
    )
    parser.add_argument(
        "--host-spec",
        type=Path,
        default=None,
        help="Path to machine_host_spec.json (default: <results-dir>/machine_host_spec.json).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Path to write the export JSON to.",
    )
    parser.add_argument(
        "--arch",
        type=str,
        default=None,
        help="Silicon arch (e.g. wormhole_b0). When omitted, derived from the board type.",
    )
    args = parser.parse_args()

    host_spec_path = args.host_spec or (args.results_dir / HOST_SPEC_FILENAME)
    host_spec = None
    if host_spec_path.exists():
        try:
            host_spec = json.loads(host_spec_path.read_text())
        except (OSError, json.JSONDecodeError) as e:
            print(f"WARN: could not read {host_spec_path}: {e}", file=sys.stderr)
    else:
        print(
            f"WARN: {host_spec_path} not found; run context will be sparse.",
            file=sys.stderr,
        )

    repo_dir = Path(__file__).resolve().parent
    document = {
        "run": build_run_context(host_spec, args.arch, repo_dir),
        "results": collect_results(args.results_dir),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(document, f, indent=2, allow_nan=False)

    print(
        f"Wrote {args.output} ({len(document['results'])} results, "
        f"card={document['run']['card']}, arch={document['run']['arch']})."
    )


if __name__ == "__main__":
    main()
