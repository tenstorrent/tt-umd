#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

"""
Trigger the tt-umd CI "Build and run all tests for multiple build types" workflow
(the same run as the Actions "Run workflow" button): choose the branch, build type(s),
how many times to repeat each card, and which cards to run.

Lives in .github/scripts/ so it stays next to the workflows it drives and can read them
by its own path regardless of the current directory.
"""

import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("error: PyYAML is required (pip install pyyaml)")

REPO = "tenstorrent/tt-umd"
WORKFLOW = "build-and-run-all-tests-multiple-builds.yml"
REUSABLE = "build-and-run-all-tests.yml"  # defines the cards job + build-type options
WORKFLOWS_DIR = Path(__file__).resolve().parent.parent / "workflows"


def load_reusable(repo_path=None):
    base = Path(repo_path) / ".github/workflows" if repo_path else WORKFLOWS_DIR
    path = base / REUSABLE
    if not path.exists():
        sys.exit(f"error: cannot find {path} (use --repo-path)")
    return yaml.safe_load(path.read_text())


def available_builds(wf):
    on = wf.get(True, wf.get("on"))  # PyYAML parses the `on:` key as the boolean True
    return list(on["workflow_dispatch"]["inputs"]["build-type"]["options"])


def available_cards(wf):
    run = "\n".join(step.get("run", "") for step in wf["jobs"]["cards"]["steps"])
    run = "\n".join(ln for ln in run.splitlines() if not ln.strip().startswith("#"))
    cards = []
    for m in re.finditer(r'"card"\s*:\s*"([^"]+)"', run):
        if m.group(1) not in cards:
            cards.append(m.group(1))
    return cards


def as_list(value):
    return [x.strip() for x in value.split(",") if x.strip()]


def current_branch():
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        )
        return out.strip()
    except Exception:
        return None


def parse_args():
    p = argparse.ArgumentParser(
        description="Start a CI run, same as the Actions 'Run workflow' button.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
examples:
  # 50x on 4 cards, Release only, on the current branch:
  repro-ci.py -r 50 --build Release \\
      --cards tt-ubuntu-2204-n150-stable,tt-ubuntu-2204-n300-stable,\\
tt-ubuntu-2204-p100a-viommu-stable,tt-ubuntu-2204-p150b-stable

  repro-ci.py --list                 # show valid build types and card names
  repro-ci.py -r 10 --cards tt-ubuntu-2204-n300-stable --dry-run   # preview only
""",
    )
    p.add_argument(
        "-b", "--branch", help="branch to run from (default: current git branch)"
    )
    p.add_argument(
        "-r",
        "--repeat",
        help="times to repeat each card, each its own job "
        "(default 1; cards x repeat must be <= 256)",
    )
    p.add_argument(
        "--cards",
        help="comma-separated EXACT card names to run (see --list). Default: all cards",
    )
    p.add_argument(
        "--build",
        help="comma-separated build types, e.g. Release,ASan (see --list). "
        "Default: the workflow's default set",
    )
    p.add_argument("--repo", default=REPO, help=f"GitHub repo (default: {REPO})")
    p.add_argument(
        "--repo-path",
        help="local repo path to read the workflow from (default: this script's repo)",
    )
    p.add_argument(
        "-l",
        "--list",
        action="store_true",
        help="list valid build types and cards, then exit",
    )
    p.add_argument(
        "--force",
        action="store_true",
        help="dispatch even if a --cards/--build value is not in --list",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="print the gh command instead of dispatching",
    )
    return p.parse_args()


def main():
    args = parse_args()
    wf = load_reusable(args.repo_path)
    builds, cards = available_builds(wf), available_cards(wf)

    if args.list:
        print("Build types:")
        print("\n".join(f"  {b}" for b in builds))
        print("Cards:")
        print("\n".join(f"  {c}" for c in cards))
        return

    branch = args.branch or current_branch()
    if not branch:
        sys.exit("error: --branch is required (not inside a git repo)")
    if args.repeat is not None and not args.repeat.isdigit():
        sys.exit("error: --repeat must be a positive integer")

    def check_known(kind, values, known):
        unknown = [v for v in values if v not in known]
        for v in unknown:
            print(
                f"error: unknown {kind} '{v}' — run --list to see valid values",
                file=sys.stderr,
            )
        if unknown and not args.force:
            sys.exit("aborting before dispatch (use --force to override)")

    # Only pass -f for options the user actually set; unset ones fall back to the workflow default.
    inputs = []
    if args.repeat is not None:
        inputs += ["-f", f"repeat-count={args.repeat}"]
    if args.cards:
        vals = as_list(args.cards)
        check_known("card", vals, cards)
        inputs += ["-f", f"cards-filter={json.dumps(vals)}"]
    if args.build:
        vals = as_list(args.build)
        check_known("build type", vals, builds)
        inputs += ["-f", f"build-types={json.dumps(vals)}"]

    cmd = [
        "gh",
        "workflow",
        "run",
        WORKFLOW,
        "--repo",
        args.repo,
        "--ref",
        branch,
        *inputs,
    ]
    print(
        f"repo={args.repo}  branch={branch}  repeat={args.repeat or '<default>'}  "
        f"build={args.build or '<default>'}  cards={args.cards or '<all>'}"
    )

    if args.dry_run or not shutil.which("gh"):
        if not args.dry_run:
            print("(gh not found — showing the command / UI steps instead)")
        print(shlex.join(cmd))
        print(
            "UI: Actions -> Build and run all tests for multiple build types -> Run workflow"
        )
        return

    result = subprocess.run(cmd)
    if result.returncode:
        sys.exit(result.returncode)
    subprocess.run(
        [
            "gh",
            "run",
            "list",
            "--repo",
            args.repo,
            "--workflow",
            WORKFLOW,
            "--limit",
            "3",
        ]
    )


if __name__ == "__main__":
    main()
