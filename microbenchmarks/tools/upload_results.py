# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Upload the nanobench results of one run to MongoDB.

Collects the run context from git and the machine, reads every nanobench ``<title>.json`` from a results directory and
stores them in two collections: ``runs`` (one document per pipeline and host, holding the run context) and ``results``
(one document per benchmark case, referencing its run through ``run_id``).

Run from the repository root as ``python -m microbenchmarks.tools.upload_results <results_dir> --pipeline-id <id>``. The
MongoDB connection string is read from the MONGODB_URI environment variable.
"""

import argparse
import json
import math
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

from pymongo import ASCENDING, DESCENDING, MongoClient
from pymongo.database import Database

from run_context import collect_run_context

DATABASE_NAME = "umd_perf"

# nanobench renders unset floating-point fields as bare `nan`/`-nan`, which Python's json parser rejects.
_NAN_PATTERN = re.compile(r"-?\bnan\b")


def format_with_unit(value: float, unit: str):
    if unit and unit != "byte":
        # If a specific unit is defined (and not "byte"), use it
        return f"{value:g} {unit}"

    # Otherwise use binary units (KiB, MiB, GiB)
    if value >= 1024**3:
        return f"{value / (1024**3):g} GiB"
    elif value >= 1024**2:
        return f"{value / (1024**2):g} MiB"
    elif value >= 1024:
        return f"{value / 1024:g} KiB"
    else:
        return f"{value:g} B"


def load_json(path: Path) -> dict:
    return json.loads(_NAN_PATTERN.sub("NaN", path.read_text()))


def load_cases(results_dir: Path) -> list:
    """Returns the cases of all nanobench JSON files in the directory."""
    cases = []
    for path in sorted(results_dir.glob("*.json")):
        cases.extend(load_json(path)["results"])
    return cases


def _is_nan(value) -> bool:
    return isinstance(value, float) and math.isnan(value)


def to_result_document(case: dict, run_id) -> dict:
    """Converts one nanobench result into a results document.

    Unavailable (NaN) values, such as disabled perf counters, are omitted. As in nanobench's console output, throughput
    is in the case's unit per second and the error is a percentage. The elapsed time is per iteration, which processes
    a whole batch.
    """
    document = {
        key: value
        for key, value in case.items()
        if key != "elapsed_error" and not _is_nan(value)
    }
    document["run_id"] = run_id
    document["throughput"] = case["batch"] / case["elapsed"]
    document["err_percent"] = 100 * case["elapsed_error"]
    unit = case["unit"]
    document["batch_formatted"] = format_with_unit(document["batch"], unit)
    document["throughput_formatted"] = (
        format_with_unit(document["throughput"], unit) + "/s"
    )
    return document


def ensure_indexes(database: Database) -> None:
    # The unique key makes concurrent uploads of the same run safe.
    database.runs.create_index(
        [("pipeline_id", ASCENDING), ("hostname", ASCENDING)], unique=True
    )
    # Serves "freshest run" and per-host history ordered by commit time.
    database.runs.create_index(
        [
            ("hostname", ASCENDING),
            ("git_branch", ASCENDING),
            ("git_commit_timestamp", DESCENDING),
        ]
    )
    database.results.create_index([("run_id", ASCENDING), ("title", ASCENDING)])


def upload(results_dir: Path, pipeline_id: int, database: Database) -> None:
    run = {
        **collect_run_context(),
        "pipeline_id": pipeline_id,
        "created_at": datetime.now(timezone.utc),
    }

    ensure_indexes(database)
    outcome = database.runs.update_one(
        {"pipeline_id": pipeline_id, "hostname": run["hostname"]},
        {"$setOnInsert": run},
        upsert=True,
    )
    if outcome.upserted_id is None:
        print(
            f"Run for pipeline {pipeline_id} on {run['hostname']} is already uploaded, skipping."
        )
        return

    run_id = outcome.upserted_id
    documents = [to_result_document(case, run_id) for case in load_cases(results_dir)]
    try:
        database.results.insert_many(documents)
    except Exception:
        # Results are only inserted for a newly created run, so a run left without its results would never be retried.
        database.runs.delete_one({"_id": run_id})
        raise
    print(f"Uploaded run {run_id} with {len(documents)} results.")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "results_dir",
        type=Path,
        help="Directory with the nanobench JSON files.",
    )
    parser.add_argument(
        "--pipeline-id",
        type=int,
        required=True,
        help="Id of the CI pipeline that produced the run.",
    )
    args = parser.parse_args()

    uri = os.environ.get("MONGODB_URI")
    if not uri:
        print("MONGODB_URI is not set.", file=sys.stderr)
        return 1
    upload(args.results_dir, args.pipeline_id, MongoClient(uri)[DATABASE_NAME])
    return 0


if __name__ == "__main__":
    sys.exit(main())
