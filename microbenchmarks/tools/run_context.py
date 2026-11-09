# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Collects the context of a benchmark run: the code that was benchmarked and the machine it ran on."""

import socket
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Optional

# Git is queried from this checkout, which is the one the benchmarks were built from.
_CHECKOUT_DIR = Path(__file__).resolve().parent
_KMD_VERSION_PATHS = [
    Path("/sys/module/tenstorrent/version"),
    Path("/sys/module/tt_kmd/version"),
]


def _git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=_CHECKOUT_DIR, text=True).strip()


def _read_kmd_version() -> Optional[str]:
    for path in _KMD_VERSION_PATHS:
        if path.exists():
            return path.read_text().strip()
    return None


def collect_run_context() -> dict:
    """Returns the run context fields. Fields that are unavailable (branch on a detached HEAD, KMD version when the
    driver is not loaded) are omitted."""
    context = {
        "git_commit": _git("rev-parse", "HEAD"),
        # Stored as a date so runs can be sorted by commit time.
        "git_commit_timestamp": datetime.fromisoformat(
            _git("show", "-s", "--format=%cI", "HEAD")
        ),
        "hostname": socket.gethostname(),
    }
    branch = _git("branch", "--show-current")
    if branch:
        context["git_branch"] = branch
    kmd_version = _read_kmd_version()
    if kmd_version:
        context["kmd_version"] = kmd_version
    return context
