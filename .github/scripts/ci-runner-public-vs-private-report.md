# tt-umd CI: big private runner vs public runner — report

**Data window:** 2026-05-01 → 2026-06-02 (29 active days)
**Sample:** 1,527 runs of the two workflows that use the big runner (*Build Device* + *Build and run all
tests for multiple build types*); **12,451** `tt-ubuntu-2204-large-stable` build jobs measured.

> A standalone, shareable HTML version (with a per-day chart) is at
> `.github/scripts/ci-runner-public-vs-private-report.html`.

## Goals

1. **Reduce total CI time.** Public is roughly break-even on a typical day and ~5 min slower in the best
   case (4-core compile). The win is at the **tail** — it removes the 20–80 min queue spikes that hit
   whenever the big-runner pool is busy, and makes run time predictable.
2. **Reduce pressure on the big private runner.** Free the scarce self-hosted pool for work that needs it
   (ttsim, hardware tests). Because the moved build jobs finish under the "ttsim floor" (below), this
   offload of ~10–13 jobs/PR is essentially free.

## TL;DR

The big runner builds fast but is **unreliable to acquire**: median wait **43 s**, but each build workflow
fires ~10 jobs at once, so on busy days the wait collapses — **p90 ~20 min, p95 ~29 min, max 82 min**, and
on the busiest day (Jun 1) the *median* was 27 min. Public `ubuntu-22.04` starts in **~3 s, every time**.

**The "ttsim floor":** every PR run contains `run-ttsim-tests` whose pure runtime is **~21 min 27 s** (it
builds all of tt-metal, the longest single job). So **any build job under ~21.5 min on public is a free
win** — it finishes before ttsim, never extending the run, even without ccache. Every moved job is under it.

## How long do we wait for the big runner?

`tt-ubuntu-2204-large-stable` build jobs (12,451 jobs, full window):

| wait for a runner | min | median | mean | p75 | p90 | p95 | max |
|---|--:|--:|--:|--:|--:|--:|--:|
| all large-stable build jobs | 0s | 43s | 6m37s | 7m04s | **19m42s** | **29m01s** | **81m44s** |

~50% start within 1 min and ~75% within 7 min — but ~10% wait over 20 min, ~5% over 29 min, worst 82 min.
Pure build work is only 3–8 min, so on bad days the wait dwarfs the work. **It's load-dependent:** near-zero
on quiet days, 20–32 min median on busy days (May 25, May 27, Jun 1). See the per-day chart in the HTML.

## Result — total run time (build-device, a pure build workflow)

| build-device total run | median | mean | p75 | p90 | max |
|---|--:|--:|--:|--:|--:|
| **Big worker** (n=781, full period) | 11m24s | 16m46s | 19m43s | **30m06s** | **89m44s** |
| **Public** (warm, all changes) | ~16m | ~16m | ~16m | ~16m | ~16m |

**Read honestly:** on a typical day the big runner is faster (median 11m vs ~16m) — its many cores chew
through clang-tidy; public pays a ~5 min 4-core penalty. Public's value is the p75/p90/max columns: a flat
~16 min versus 20 / 30 / 90 min. It trades a slightly-slower good day for eliminating the bad days.

## Per-job detail (warm)

| Build job | Big work | Public work | ccache hit | Under 21.5m floor? |
|---|--:|--:|--:|---|
| `Build` umd_tests | 3m34s | 1m12s–6m | 66% (96% fully warm) | yes — free |
| `Build umd wheel` | 4m19s | ~6m | **100%** | yes — free |
| `Build device` (per variant) | 7m24s | 10–14m | 90% | yes |
| manylinux wheel | 5m09s | 8m16s | — | yes |
| ttsim (builds all tt-metal) | ~21m27s | doesn't fit 4-core | — | it IS the floor → keep on big |

build-device stays ~10–14m even warm because it runs **clang-tidy** (not cacheable) on 4 cores; ccache hits
90% on the compile but can't touch clang-tidy. Lever = larger public runner or a separate clang-tidy job.

## What we changed

1. **Runner swap** to public `ubuntu-22.04` (build-device image repointed Harbor → public ghcr).
2. **ccache** on the build jobs + optional ccache for the wheel (env-gated, with `CCACHE_BASEDIR`/`NOHASHDIR`).
3. **PCH** for heavy externals + gtest, gated to clang-tidy-off builds.
4. **Forward-declaration / include cleanup** (~80 includes pruned from ~40 headers; verified clang-20 + gcc-13).
5. **Shell fix** (build-device default shell → bash; `sh` was silently building the heavy branch).

## Conclusions

- **Goal 2 (offload pressure): achieved, effectively free.** Moved jobs finish under the ttsim floor →
  offloading ~10–13 jobs/PR extends no run; holds even without ccache. Strongest, lowest-risk outcome.
- **Goal 1 (faster total time): a tail & predictability win, not a blanket speedup.** Public ≈ big on a
  typical day (build-device 16m vs 11m median); the benefit is removing the 20–90 min spikes (flat ~16m).
- **Scope note:** a full PR run (with hardware tests) is gated by `test-all` waiting for scarce silicon
  (~85 min), not by builds. These changes improve build-feedback latency and free the big runner; they
  don't shrink the full test run — that's a test-sharding / hardware-capacity question.

## Recommendations

1. Move `Build` and `Build umd wheel` to public with ccache — free win (under the floor), predictable.
2. Move `Build device` to public for predictability + offload; accept the ~5 min typical-day cost, or pair
   with a larger public runner if its ~14 min matters.
3. Keep PCH gated to clang-tidy-off builds; keep ttsim and hardware tests on their current runners.

## Methodology

`queue = started_at − created_at`, `work = completed_at − started_at` from the GitHub Actions jobs API.
1,527 runs over 2026-05-01 → 2026-06-02; 12,451 jobs filtered to `tt-ubuntu-2204-large-stable`; re-run
timestamp artifacts (negative queue) dropped. Public figures from warm runs 26812621709 / 26812642845 on
branch `ci/build-runner-public-comparison`. Code changes verified to compile on clang-20 + gcc-13.
