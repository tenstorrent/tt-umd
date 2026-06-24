# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Shared helpers for microbenchmark tooling.

Lives in its own module so consumers that only need these lightweight helpers
(e.g. summarize_regressions.py) don't have to pull in analyze_results.py's
pandas/psutil dependencies.
"""


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


def yaml_escape(s: str) -> str:
    """Wrap a string in quotes if it contains characters that confuse YAML."""
    if any(c in s for c in ":#,&*!|>'\"%@`{}[]\n") or s.strip() != s:
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s
