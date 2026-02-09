# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

from collections import defaultdict
import os
import json
import platform
import socket
import psutil
import pandas as pd
import argparse
import sys
from pathlib import Path
from datetime import datetime


def format_throughput(throughput, unit):
    """Format throughput with appropriate units (bytes/s, KB/s, MB/s, GB/s)"""
    if unit and unit != "byte":
        # If a specific unit is defined (and not "byte"), use it
        return f"{throughput:.2f} {unit}/s"

    # Otherwise use binary units (KiB, MiB, GiB)
    if throughput >= 1024**3:
        return f"{throughput / (1024**3):.2f} GiB/s"
    elif throughput >= 1024**2:
        return f"{throughput / (1024**2):.2f} MiB/s"
    elif throughput >= 1024:
        return f"{throughput / 1024:.2f} KiB/s"
    else:
        return f"{throughput:.2f} B/s"


def format_time(time_sec):
    """Format time with appropriate units (s, ms, μs, ns)"""
    if time_sec >= 1:
        return f"{time_sec:.4f} s"
    elif time_sec >= 1e-3:
        return f"{time_sec * 1e3:.4f} ms"
    elif time_sec >= 1e-6:
        return f"{time_sec * 1e6:.4f} μs"
    else:
        return f"{time_sec * 1e9:.4f} ns"


def process_benchmark(base_json_path: Path, compare_json_path=None):
    with open(base_json_path, "r") as f:
        base_json = json.load(f)
    compare_json = None
    if compare_json_path:
        with open(compare_json_path, "r") as f:
            compare_json = json.load(f)

    # Create a lookup dict for compare results by name
    compare_lookup = {}
    if compare_json:
        for result in compare_json["results"]:
            compare_lookup[result.get("name")] = result

    data = []
    for result in base_json["results"]:
        throughput = result.get("batch") / result.get("median(elapsed)")
        unit = result.get("unit")
        name = result.get("name")

        row = {
            "Name": name,
            "Batch size": result.get("batch"),
            "Throughput": format_throughput(
                throughput, unit if unit != "byte" else None
            ),
            "Total Time": format_time(result.get("totalTime")),
        }

        # Add comparison if available
        if name in compare_lookup:
            compare_result = compare_lookup[name]
            compare_throughput = compare_result.get("batch") / compare_result.get(
                "median(elapsed)"
            )
            compare_total_time = compare_result.get("totalTime")
            throughput_change = (
                (throughput - compare_throughput) / compare_throughput
            ) * 100

            row["Throughput (Old)"] = format_throughput(
                compare_throughput, unit if unit != "byte" else None
            )
            row["Total Time (Old)"] = format_time(compare_total_time)
            row["Difference %"] = f"{throughput_change:+.2f}%"

        data.append(row)

    df = pd.DataFrame(data)
    return df


def main():
    parser = argparse.ArgumentParser(description="Analyze UMD benchmark results.")
    parser.add_argument(
        "base_path", help="Path containing benchmark results.", type=Path
    )
    parser.add_argument(
        "-c",
        "--compare_path",
        help="Path contaning benchmark results to compare base results.",
        type=Path,
        required=False,
    )
    args = parser.parse_args()

    base_path = args.base_path
    compare_path = args.compare_path
    if not base_path.is_dir():
        print("Base path is not a directory: {}", base_path.str())
        exit(1)
    if compare_path and not compare_path.is_dir():
        print("Compare path is not a directory: {}", base_path.str())
        exit(1)

    benchmarks = base_path.glob("*.json")
    for benchmark in sorted(benchmarks):
        if benchmark.name == "machine_host_spec.json":
            continue

        df = process_benchmark(
            benchmark, (compare_path / benchmark.name) if compare_path else None
        )
        print(f"\n## {benchmark.stem}\n")
        print(df.to_markdown(index=False))


if __name__ == "__main__":
    main()
