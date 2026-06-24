# Microbenchmark regression-detection tools

These scripts implement the perf regression pipeline: CI runs the benchmarks
on dedicated runners, the resulting per-arch JSONs are compared against
in-repo baselines (`tests/microbenchmark/baselines/<arch>.yaml`),
and a markdown summary is published to the workflow's Step Summary tab.

The three scripts you'll touch:

| Script | Role |
|---|---|
| [`update_baseline.py`](update_baseline.py) | Update a per-arch baseline YAML from a CI artifact. Run when you want to (re)calibrate. |
| [`summarize_regressions.py`](summarize_regressions.py) | What CI's `regression-check` job runs. Compares all archs in one pass and writes the markdown summary. You rarely run this by hand. |
| [`compare_to_baseline.py`](compare_to_baseline.py) | **Legacy.** Predates the move to CI-calibrated baselines (originally - tool to compare local benchmark runs on one dedicated runner against locally-calibrated baselines). Likely to be removed or merged into `summarize_regressions.py`. See caveat below. |

## `update_baseline.py` — re-calibrate one arch from a CI artifact

When to use: after an intentional perf change merges, after hardware changes,
or when new bench cases are added and you want the baseline to reflect them.

```bash
# Pick a recent successful run of build-and-run-all-benchmarks.yml on main:
gh run download <run-id> \
    --name benchmark-json-wormhole_b0-tt-ubuntu-2204-n150-viommu-stable-ubuntu-22.04 \
    --dir /tmp/ci-bench

python3 tests/microbenchmark/tools/update_baseline.py \
    --arch 'WH n150' \
    --from-results-dir /tmp/ci-bench

# inspect the diff, commit wh_n150.yaml.
git diff tests/microbenchmark/baselines/wh_n150.yaml
git add tests/microbenchmark/baselines/wh_n150.yaml
git commit -m "Recalibrate WH n150 baseline from CI run <run-id>"
```

Behavior:

- **Every case in the artifact is written** into the YAML (existing cases get
  fresh values + their `gate: true` flag preserved; new cases get inserted).
- Cases present in the existing YAML but **not** in the artifact (e.g.
  partial gtest-filtered runs) keep their old values with a
  `# no new result this calibration` annotation.
- To exclude a case from regression-check, delete its line by hand. It'll
  reappear next time you run the script against an artifact that contains
  it — manual deletion is the only filter.
- `--dry-run` prints the would-be YAML without writing.

Tolerance: `tolerance_pct = max(5, ceil(MAPE_K * mape_pct))` where `mape_pct`
is nanobench's `medianAbsolutePercentError(elapsed)` for that case. `MAPE_K=1`
is the current setting — raise to ~2.5 if CI false-positives start showing up.

## `summarize_regressions.py` — what CI runs

The downstream step in
[`.github/workflows/regression-check.yml`](../../../.github/workflows/regression-check.yml).
It downloads all `benchmark-json-*` artifacts from the current workflow run,
compares each case's throughput against the stored
`median_throughput ± tolerance_pct` in the matching arch's YAML, and writes
markdown to `$GITHUB_STEP_SUMMARY`.

You'd run it locally only for debugging the CI step itself:

```bash
python3 tests/microbenchmark/tools/summarize_regressions.py \
    --current /tmp/all-arch-results \
    --baselines-dir tests/microbenchmark/baselines \
    --output /tmp/summary.md
```

Exit code: `0` always, except when a `gate: true` case breaches DOWN — then
exit `1` so CI fails. Non-gated breaches are soft alerts; they show up in
the summary but don't fail the job.

## `compare_to_baseline.py` — legacy local comparator

**Leftover from an earlier design.** This script was written when baselines
were themselves populated from local benchmark runs — an apples-to-apples
local-vs-local comparison on the same shell, same binary, same invocation
pattern. Since baselines moved to being calibrated from dedicated CI runners
(see `update_baseline.py`), this comparison is no longer apples-to-apples:
local runs and CI runs diverge by an unmeasured amount even on the same
physical hardware (see caveat below).

Likely future: either removed outright, or folded into `summarize_regressions.py`
as an optional single-arch view that operates on a downloaded CI artifact.

For now it still works as a wrapper around `summarize_regressions.py`. Takes
a local results directory (typically `$UMD_MICROBENCHMARK_RESULTS_PATH` from
a hand-run of the benchmark binary) and produces a single-arch table.

```bash
export UMD_MICROBENCHMARK_RESULTS_PATH=/tmp/umd-bench
mkdir -p "$UMD_MICROBENCHMARK_RESULTS_PATH"
./build/test/umd/microbenchmark/umd_microbenchmark \
    --gtest_filter='MicrobenchmarkOpenCluster.ClusterConstructor'

python3 tests/microbenchmark/tools/compare_to_baseline.py --arch 'WH n150'
```

### ⚠️ Caveat: this is a smoke test, not a precise regression check

Even when you run this on the same machine that produces the CI baseline
(e.g. SSH'd into `bgd-lab-06`), the local run won't reproduce CI's numbers
exactly:

- **Different process invocation.** CI starts the binary inside its own
  container with a clean state; your local run starts from whatever state
  your shell session left the runner in. Page cache, thermal state, and
  scheduler state at iteration zero are not identical.
- **Different container image.** If you're not running inside CI's exact
  Docker image (`ghcr.io/tenstorrent/tt-umd/tt-umd-ci-ubuntu-22.04:latest`)
  with the same volume mounts and the same compiler-built binary, userspace
  differences add a few percent of drift.
- **Other concurrent activity.** CI runs the bench on an otherwise-idle
  runner. If your SSH session is doing anything else (compilation, file IO,
  another bench), measurements skew.

Use this for: "I think I just broke something obvious, let me see ±10%."



## Pipeline overview

```
build-and-run-all-benchmarks.yml         (orchestrator, matrix per arch)
   ├─ build-tests           → uploads compiled binary artifact
   ├─ run-benchmarks (×N)   → on dedicated runner per arch
   │                          (bgd-lab-06 for WH n150, etc.)
   │                          uploads benchmark-json-<arch>-... artifact
   ├─ analyze-results       → per-arch drift-vs-latest-main (existing)
   └─ regression-check      → downloads all benchmark-json-*,
                              runs summarize_regressions.py,
                              writes markdown to Step Summary

(separately, occasionally)
update_baseline.py  ←  benchmark-json-* artifact  →  baselines/<arch>.yaml
                                                     (PR, review, merge)
```
