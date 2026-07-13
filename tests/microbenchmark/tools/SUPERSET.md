# Publishing microbenchmark results to Superset

This is the automated replacement for pasting perf numbers into per-benchmark
READMEs (issue #1534). Nightly, CI converts that run's nanobench results into the
Tenstorrent perf-data schema and uploads them to the Data team's perf ingest;
from there they land in a database and are visualized in
[Superset](https://superset.tenstorrent.com).

## How the data flows

```
build-and-run-all-benchmarks.yml (nightly cron 02:00 UTC / manual dispatch)
  run-benchmarks         → benchmark-json-<arch>-… artifacts (nanobench *.json + machine_host_spec.json)
  upload-superset        → upload-benchmark-superset.yml:
       emit_benchmark_report.py   → benchmark_<pipeline>_<arch>_<ts>.json   (CompleteBenchmarkRun schema)
       sftp                       → perf ingest endpoint (SFTP_PERF_WRITER_*)
                                        │
                          Data team's `benchmark` data_airflow pipeline
                                        │  ingest → wrangle → upsert
                                        ▼
                    sw_test.benchmark_run  +  sw_test.benchmark_measurement   (Postgres/Timescale → Snowflake)
                                        │
                                        ▼
                    Superset dataset (saved SQL)  →  charts  →  dashboard
```

We reuse the **shared** SW-perf pipeline that tt-metal / tt-forge / tt-xla already
feed, rather than standing up a UMD-specific table. No new database table or
`data_airflow` pipeline is required — only a Superset **dataset** (a saved SQL
query, see below).

## What maps to what

| `benchmark_run` / `benchmark_measurement` column | UMD source |
|---|---|
| `run_type` | `"umd_microbenchmark"` |
| `ml_model_name` *(NOT NULL)* | `"umd_microbenchmark"` — UMD has no model; the column is reused as a suite-family label |
| `device_hostname` *(NOT NULL)* | `host_info.CI_Runner` (runner name) |
| `device_info` (JSONB) | full `machine_host_spec.json` — `BoardType`, PCIe lanes, CPU governor, driver, … |
| `git_*`, `github_pipeline_*` | GitHub Actions context |
| `measurement.step_name` | nanobench `<title>` — the *test* (`TLB_DRAM`, `PCIe_DMA`, …) |
| `measurement.name` | `"<case> \| <metric>"` — case + metric, since a row holds one value (e.g. `"Static TLB, write, 1024 bytes \| throughput"`) |
| `measurement.value` | the metric's value (see below) |
| `measurement.iteration` | always `1` — per-epoch samples not exported; `err_pct` summarizes the within-run spread |
| `measurement.target` | `null` for now — baselines are intentionally not consumed; charts show plain current results |

Metric rows emitted per case (split `name` on `" | "` to recover case + metric):

| metric suffix | value | from nanobench |
|---|---|---|
| `throughput` | `batch / median(elapsed)` (byte/s for byte-unit benches) | `batch`, `median(elapsed)` |
| `median_elapsed_s` | median per-iteration elapsed, seconds | `median(elapsed)` |
| `total_time_s` | total wall time, seconds (the README "total" column) | `totalTime` |
| `err_pct` | within-run error %, `MAPE × 100` | `medianAbsolutePercentError(elapsed)` |
| `epochs` | how many epochs ran for this case (count) | `epochs` |
| `iterations` | total iterations across all epochs (count) | Σ `measurements[].iterations` |

Filter every UMD chart on `git_repo_name = 'tt-umd' AND run_type = 'umd_microbenchmark'`
to isolate our rows from the other teams sharing the table.

---

## Step 1 — what to request from the Data team (#help-data)

The CI side is done; these are the things only the Data team / a repo admin can do.

1. **Confirm the `benchmark` pipeline is live.** The perf ingest → `sw_test.benchmark_run`
   pipeline exists, but its data was seen stale in mid-2026. Ask them to confirm the
   perf SFTP ingest bucket and the `benchmark` DAG are actively loading, so our uploads
   are actually picked up. *(This is the one real prerequisite — verify it first.)*

2. **Provision an SFTP writer credential** for the perf/benchmark ingest for tt-umd CI,
   and give us:
   - the perf SFTP **hostname** (the benchmark-writer AWS Transfer endpoint),
   - the **username** (e.g. `benchmark-writer`),
   - an **SSH private key** authorized for that user.

3. **Say which DB connection to build the Superset dataset on** — the legacy
   Postgres/Timescale or the new Snowflake (a migration is in progress). This only
   changes the JSON-extraction syntax in the SQL below.

4. **Sanity-check the mapping** — confirm they're fine with
   `run_type='umd_microbenchmark'` / `ml_model_name='umd_microbenchmark'` as UMD's
   markers (see *Downsides* for why `ml_model_name` is abused). Also worth looping in
   whoever currently owns the `benchmark_run` dataset, since it's being actively extended.

## Step 2 — add the GitHub repo secrets (repo admin)

Settings → Secrets and variables → Actions → New repository secret:

| Secret | Value |
|---|---|
| `SFTP_PERF_WRITER_HOSTNAME` | perf SFTP endpoint hostname |
| `SFTP_PERF_WRITER_USERNAME` | SFTP user (e.g. `benchmark-writer`) |
| `SFTP_PERF_WRITER_KEY` | the SSH private key (full PEM contents) |

Until these exist, `upload-benchmark-superset.yml` still runs on the nightly schedule: it emits the
JSON and attaches it as the `benchmark-superset-json` artifact, and simply skips the
SFTP step — so it is safe to merge now and light up later.

## Step 3 — create the Superset dataset + charts

In Superset (SQL Lab → run → **Save dataset**, or Datasets → **+ Dataset** as a
virtual/SQL dataset), against the connection from step 1.3.

**Dataset A — time series (one row per case per run):**

```sql
SELECT
    r.run_start_ts,
    r.git_commit_ts,
    r.git_commit_hash,
    r.git_branch_name,
    r.github_pipeline_link,
    r.device_hostname,
    r.device_info->>'BoardType'   AS arch,   -- Snowflake: device_info:BoardType::string
    m.step_name                   AS test,
    split_part(m.name, ' | ', 1)  AS case_name,
    split_part(m.name, ' | ', 2)  AS metric,  -- throughput | median_elapsed_s | total_time_s | err_pct
    m.value                       AS value
FROM sw_test.benchmark_run r
JOIN sw_test.benchmark_measurement m USING (benchmark_run_id)
WHERE r.git_repo_name = 'tt-umd'
  AND r.run_type    = 'umd_microbenchmark';
```

**Dataset B — latest value per case** (for "current stats" tables/big-numbers):

```sql
SELECT * FROM (
  SELECT
      r.device_info->>'BoardType' AS arch,
      m.step_name                  AS test,
      split_part(m.name, ' | ', 1) AS case_name,
      split_part(m.name, ' | ', 2) AS metric,
      m.value                      AS value,
      r.git_commit_hash,
      r.run_start_ts,
      ROW_NUMBER() OVER (
          PARTITION BY r.device_info->>'BoardType', m.step_name, m.name
          ORDER BY r.run_start_ts DESC) AS rn
  FROM sw_test.benchmark_run r
  JOIN sw_test.benchmark_measurement m USING (benchmark_run_id)
  WHERE r.git_repo_name = 'tt-umd'
    AND r.run_type    = 'umd_microbenchmark'
) t
WHERE rn = 1;
```

### The chart you asked for — one test, a coloured line per case, over commits

Yes, this is directly supported. On **Dataset A**:

- Chart type: **Line Chart** (time-series)
- **X-axis / time column**: `run_start_ts` (or `git_commit_ts`) — orders the points by
  commit chronologically; surface `git_commit_hash` as the point label / tooltip so the
  x-axis reads as successive commit SHAs
- **Metric**: `MAX(value)`
- **Dimensions (series)**: `case_name`  → one differently-coloured line per case
- **Filters**: `test = 'TLB_DRAM'`, `arch = 'n150'`, **`metric = 'throughput'`**

The `metric` filter picks which number the chart plots — swap it for `median_elapsed_s`,
`total_time_s`, or `err_pct` to chart those instead. Each case (e.g. every transfer size)
becomes its own line; the x-axis walks the commits/runs over time. No target/baseline
line — just the plain current value per case. Duplicate the chart per test (`TLB_DRAM`,
`TLB_Tensix`, `PCIe_DMA`, `IOMMU`, …), or add dashboard filters on `test`, `arch`, and
`metric` so one chart serves all of them. For "latest stats per case", build a **Table**
chart on Dataset B with dimensions `test`, `case_name`, `metric` and metric `MAX(value)`.

---

## Downsides of reusing the shared tables (honest assessment)

Reuse is acceptable for v1 — none of these are blockers — but know the trade-offs:

1. **`ml_model_name` is `NOT NULL`.** UMD has no model, so we set it to
   `"umd_microbenchmark"`. Cosmetic, but our rows carry a slightly nonsensical column.
2. **The table is shared** with tt-metal/forge/xla ML data. Every dataset/chart **must**
   filter `git_repo_name='tt-umd'` or it will pull in unrelated rows. Discipline, not a limitation.
3. **ML-specific columns are dead weight** for us (`num_layers`, `batch_size`,
   `input_sequence_length`, `image_dimension`, `training`, …) — all NULL. Harmless.
4. **The schema is owned and evolved by the Data team** for ML benchmarking (columns are
   actively being added). We're a passenger; additions are backward-compatible, but coordinate.
5. **Snowflake migration in progress.** Build the dataset on the connection they point you
   to, or a chart may be migrated out from under you.
6. **Pipeline liveness** (see request #1) is the only genuine risk — confirm the DAG ingests.
7. **No target/baseline line for now** — `target` is emitted as `null` by choice; charts
   show plain current throughput per case over commits. To add a target line later,
   populate `target` from `baselines/<arch>.yaml` in `emit_benchmark_report.py`.

A dedicated UMD pipeline/table would only pay off if our needs diverge from the shared
schema — e.g. if we want transfer size / direction / TLB-kind as first-class queryable
columns instead of encoded inside the case name. That's a larger effort (a
`data_airflow` PR + new S3 buckets + new SFTP user + Data-team review) and not worth it
until a chart actually needs it.

## Local dry-run

```bash
# point --current at a tree of downloaded benchmark-json-* artifacts
PYTHONPATH=tests/microbenchmark/tools \
python3 tests/microbenchmark/tools/emit_benchmark_report.py \
    --current /tmp/all-arch-results \
    --output-dir /tmp/superset-json --dry-run
```
