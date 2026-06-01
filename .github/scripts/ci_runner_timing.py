#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

"""Analyze GitHub Actions runner queue time vs. work time for tt-umd CI.

Motivation
----------
Self-hosted autoscaled runners (e.g. ``tt-ubuntu-2204-large-stable``) build fast
but can take a long time to *acquire* — a job may wait 20+ minutes for a machine
to do ~3 minutes of work, and build jobs gate the whole downstream test pipeline.
This script quantifies that, so you can decide which jobs are worth moving to
near-instant public GitHub-hosted runners (``ubuntu-22.04`` queues in ~3s).

Definitions (from the GitHub Actions jobs API)
----------------------------------------------
Each job exposes three timestamps:
  created_at    : job enters the queue (AFTER its ``needs:`` are satisfied — verified
                  empirically: a build job's created_at equals the second its
                  ``ensure-image`` dependency completed).
  started_at    : a runner picked the job up.
  completed_at  : the job finished.

  queue time = started_at  - created_at   -> pure wait-for-a-runner
  work  time = completed_at - started_at  -> execution (incl. runner setup + container init)

Re-run caveat: for re-run workflows the API returns the re-run's created_at but the
original attempt's started_at, yielding large negative queue times. Those records
(queue < 0) are dropped.

Requirements: Python 3.8+, the ``gh`` CLI authenticated with repo read access.

Usage
-----
  # Baseline: last 250 runs of the active multi-build CI, broken down by the large runner
  ./ci_runner_timing.py

  # A different workflow / sample size / focus runner
  ./ci_runner_timing.py --workflow build-and-run-all-tests.yml --runs 100 \
                        --focus-label tt-ubuntu-2204-large-stable

  # Inspect one specific run (e.g. the experiment PR run) to compare against the baseline
  ./ci_runner_timing.py --run-id 26759202979

  # Save the raw job records for ad-hoc analysis
  ./ci_runner_timing.py --json /tmp/umd_ci_jobs.json
"""

import argparse
import json
import statistics as st
import subprocess
import sys
import re
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime

DEFAULT_REPO = "tenstorrent/tt-umd"
DEFAULT_WORKFLOW = "build-and-run-all-tests-multiple-builds.yml"
DEFAULT_FOCUS = "tt-ubuntu-2204-large-stable"
PUBLIC_LABELS = {"ubuntu-22.04", "ubuntu-latest", "ubuntu-24.04", "ubuntu-20.04"}


# --------------------------------------------------------------------------- gh
def gh_api(path):
    out = subprocess.run(["gh", "api", path], capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write(f"gh api {path} failed: {out.stderr.strip()}\n")
        return None
    return json.loads(out.stdout)


def parse_ts(t):
    return datetime.strptime(t, "%Y-%m-%dT%H:%M:%SZ") if t else None


# ----------------------------------------------------------------------- fetch
def list_runs(repo, workflow, n_runs):
    runs, page = [], 1
    while len(runs) < n_runs:
        d = gh_api(
            f"repos/{repo}/actions/workflows/{workflow}/runs?per_page=100&page={page}"
        )
        if not d or not d.get("workflow_runs"):
            break
        for r in d["workflow_runs"]:
            runs.append(
                {"id": r["id"], "event": r["event"], "created_at": r["created_at"]}
            )
        if len(d["workflow_runs"]) < 100:
            break
        page += 1
    return runs[:n_runs]


def jobs_for_run(repo, run):
    recs, page = [], 1
    while True:
        d = gh_api(
            f"repos/{repo}/actions/runs/{run['id']}/jobs?per_page=100&page={page}"
        )
        if not d or not d.get("jobs"):
            break
        for j in d["jobs"]:
            c, s, e = (
                parse_ts(j.get("created_at")),
                parse_ts(j.get("started_at")),
                parse_ts(j.get("completed_at")),
            )
            recs.append(
                {
                    "run_id": run["id"],
                    "event": run.get("event"),
                    "name": j["name"],
                    "label": j["labels"][0] if j.get("labels") else "(none)",
                    "conclusion": j.get("conclusion"),
                    "queue_s": (s - c).total_seconds() if c and s else None,
                    "work_s": (e - s).total_seconds() if s and e else None,
                }
            )
        if len(d["jobs"]) < 100:
            break
        page += 1
    return recs


def gather(repo, runs, workers):
    all_jobs = []
    with ThreadPoolExecutor(max_workers=workers) as ex:
        for i, recs in enumerate(ex.map(lambda r: jobs_for_run(repo, r), runs)):
            all_jobs.extend(recs)
            if (i + 1) % 25 == 0:
                sys.stderr.write(f"  ...{i+1}/{len(runs)} runs, {len(all_jobs)} jobs\n")
    return all_jobs


# ---------------------------------------------------------------------- format
def fmt(s):
    if s is None:
        return "-"
    neg = s < 0
    m, sec = divmod(int(round(abs(s))), 60)
    out = f"{m}m{sec:02d}s" if m else f"{sec}s"
    return ("-" + out) if neg else out


def pct(xs, p):
    xs = sorted(xs)
    if not xs:
        return None
    k = (len(xs) - 1) * p / 100
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def stat_row(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    return {
        "n": len(xs),
        "median": st.median(xs),
        "mean": st.mean(xs),
        "p90": pct(xs, 90),
        "p95": pct(xs, 95),
        "max": max(xs),
    }


def leaf(name):
    """Last ' / ' segment with matrix parentheses stripped, to group across build types."""
    part = name.split(" / ")[-1]
    return re.sub(r"\s*\([^)]*\)", "", part).strip() or name


def valid_queue(jobs):
    """Jobs that ran on a runner, with a sane (non-negative) queue time."""
    return [
        j
        for j in jobs
        if j["label"] != "(none)" and j["queue_s"] is not None and j["queue_s"] >= 0
    ]


def valid_work(jobs):
    return [
        j["work_s"]
        for j in jobs
        if j["work_s"] is not None
        and j["work_s"] >= 0
        and j["conclusion"] in ("success", "failure")
    ]


# ----------------------------------------------------------------------- views
def print_by_label(jobs):
    print("=" * 104)
    print("QUEUE TIME (wait for runner) & WORK TIME, grouped by RUNNER LABEL")
    print("=" * 104)
    groups = defaultdict(lambda: {"q": [], "w": []})
    for j in jobs:
        groups[j["label"]]["q"].append(j["queue_s"])
    for j in jobs:
        groups[j["label"]]["w"] = groups[j["label"]]["w"]
    # work collected separately to respect conclusion filter
    work_by = defaultdict(list)
    for j in jobs:
        work_by[j["label"]] += [w for w in valid_work([j])]
    hdr = (
        f"{'runner label':42s} {'n':>5s} | {'q-median':>9s} {'q-mean':>8s} {'q-p90':>8s} "
        f"{'q-p95':>8s} {'q-max':>8s} | {'w-median':>9s} {'w-mean':>8s}"
    )
    print(hdr)
    print("-" * len(hdr))
    rows = []
    for lbl, d in groups.items():
        q = stat_row(d["q"])
        if q:
            rows.append((lbl, q, stat_row(work_by[lbl])))
    for lbl, q, w in sorted(rows, key=lambda r: -r[1]["mean"]):
        print(
            f"{lbl:42s} {q['n']:5d} | {fmt(q['median']):>9s} {fmt(q['mean']):>8s} {fmt(q['p90']):>8s} "
            f"{fmt(q['p95']):>8s} {fmt(q['max']):>8s} | "
            f"{fmt(w['median']) if w else '-':>9s} {fmt(w['mean']) if w else '-':>8s}"
        )


def print_focus(jobs, focus):
    foc = [j for j in jobs if j["label"] == focus]
    if not foc:
        print(f"\n(no jobs found on focus label '{focus}')")
        return
    print("\n" + "=" * 104)
    print(f"{focus}  —  breakdown by job (normalized leaf name)")
    print("=" * 104)
    groups = defaultdict(list)
    for j in foc:
        groups[leaf(j["name"])].append(j)
    hdr = (
        f"{'job (leaf)':46s} {'n':>5s} | {'q-median':>9s} {'q-mean':>8s} {'q-p90':>8s} | "
        f"{'w-median':>9s} {'w-mean':>8s} {'w-p90':>8s}"
    )
    print(hdr)
    print("-" * len(hdr))
    for nm, js in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        q = stat_row([j["queue_s"] for j in js])
        w = stat_row(valid_work(js))
        print(
            f"{nm[:46]:46s} {q['n']:5d} | {fmt(q['median']):>9s} {fmt(q['mean']):>8s} {fmt(q['p90']):>8s} | "
            f"{fmt(w['median']) if w else '-':>9s} {fmt(w['mean']) if w else '-':>8s} {fmt(w['p90']) if w else '-':>8s}"
        )

    # Headline: queue's share of build-phase wall-clock, and public-runner comparison
    q = stat_row([j["queue_s"] for j in foc])
    w = stat_row(valid_work(foc))
    print("\n" + "-" * 104)
    print(f"{focus}: {q['n']} jobs")
    print(
        f"  queue: median {fmt(q['median'])}  mean {fmt(q['mean'])}  p90 {fmt(q['p90'])}  p95 {fmt(q['p95'])}  max {fmt(q['max'])}"
    )
    if w:
        print(
            f"  work : median {fmt(w['median'])}  mean {fmt(w['mean'])}  p90 {fmt(w['p90'])}"
        )
    pub = [j for j in jobs if j["label"] in PUBLIC_LABELS]
    if pub:
        pq = stat_row([j["queue_s"] for j in pub])
        print(
            f"  public runners ({'/'.join(sorted(PUBLIC_LABELS & {j['label'] for j in pub}))}): "
            f"{pq['n']} jobs, queue median {fmt(pq['median'])}  p90 {fmt(pq['p90'])}"
        )
    total_q = sum(j["queue_s"] for j in jobs)
    foc_q = sum(j["queue_s"] for j in foc)
    if total_q:
        print(
            f"  share of all runner-wait in sample: {foc_q/total_q*100:.0f}%  "
            f"({foc_q/3600:.0f}h of {total_q/3600:.0f}h)"
        )


def print_single_run(repo, run_id, focus):
    run = {"id": run_id, "event": "?"}
    jobs = jobs_for_run(repo, run)
    if not jobs:
        print(f"No jobs found for run {run_id}")
        return
    vq = valid_queue(jobs)
    print("=" * 104)
    print(f"SINGLE RUN {run_id} — per-job queue & work")
    print("=" * 104)
    hdr = f"{'job':70s} {'queue':>8s} {'work':>8s}  label"
    print(hdr)
    print("-" * 96)
    for j in sorted(jobs, key=lambda j: -(j["queue_s"] or 0)):
        if j["label"] == "(none)":
            continue
        print(
            f"{j['name'][:70]:70s} {fmt(j['queue_s']):>8s} {fmt(j['work_s']):>8s}  {j['label']}"
        )
    print()
    print_by_label(vq)
    print_focus(vq, focus)


# ------------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument(
        "--workflow",
        default=DEFAULT_WORKFLOW,
        help="workflow file name or numeric id (default: %(default)s)",
    )
    ap.add_argument(
        "--runs",
        type=int,
        default=250,
        help="recent runs to sample (default: %(default)s)",
    )
    ap.add_argument(
        "--focus-label", default=DEFAULT_FOCUS, help="runner label to break down by job"
    )
    ap.add_argument(
        "--run-id",
        type=int,
        action="append",
        help="analyze one specific run instead of sampling (repeatable)",
    )
    ap.add_argument("--workers", type=int, default=10, help="parallel gh fetchers")
    ap.add_argument("--json", help="also dump raw job records to this path")
    args = ap.parse_args()

    if args.run_id:
        for rid in args.run_id:
            print_single_run(args.repo, rid, args.focus_label)
            print()
        return

    sys.stderr.write(
        f"Listing up to {args.runs} runs of {args.workflow} in {args.repo}...\n"
    )
    runs = list_runs(args.repo, args.workflow, args.runs)
    if not runs:
        sys.exit("No runs found — check --repo / --workflow.")
    sys.stderr.write(
        f"Got {len(runs)} runs ({runs[-1]['created_at']} .. {runs[0]['created_at']}); fetching jobs...\n"
    )
    jobs = gather(args.repo, runs, args.workers)
    if args.json:
        json.dump({"jobs": jobs}, open(args.json, "w"))
        sys.stderr.write(f"Wrote raw records to {args.json}\n")

    ev = defaultdict(int)
    for r in runs:
        ev[r["event"]] += 1
    dropped = sum(1 for j in jobs if j["label"] != "(none)" and (j["queue_s"] or 0) < 0)
    print(
        f"Runs by event: {dict(ev)} | total {len(runs)} runs, {len(jobs)} job records "
        f"({dropped} re-run artifacts dropped)\n"
    )

    vq = valid_queue(jobs)
    print_by_label(vq)
    print_focus(vq, args.focus_label)


if __name__ == "__main__":
    main()
