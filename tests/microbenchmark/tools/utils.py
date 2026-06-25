# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Shared helpers for microbenchmark tooling.

Lives in its own module so consumers that only need these lightweight helpers
(e.g. summarize_regressions.py) don't have to pull in analyze_results.py's
pandas/psutil dependencies.
"""

import json
import re

ARCH_NAMES = ["n150", "n300", "p150"]

# nanobench renders unset floating-point fields as bare `-nan`/`nan`, which
# Python's json parser rejects (it only accepts the capitalized `NaN`).
_NAN_RE = re.compile(r"-?\bnan\b")


def load_nanobench_json(path):
    """Read a nanobench JSON file, normalizing bare `nan`/`-nan` to `NaN`.

    Raises OSError / json.JSONDecodeError on failure for the caller to handle.
    """
    return json.loads(_NAN_RE.sub("NaN", path.read_text()))


def arch_label_from_string(s):
    """Return the first ARCH_NAMES label that appears in `s`, else None.

    Recovers the arch from a card/runner label or artifact name, e.g.
    "n150-umd-perf" -> "n150", "benchmark-json-wormhole_b0-n300-..." -> "n300".
    """
    for arch in ARCH_NAMES:
        if arch in s:
            return arch
    return None


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
