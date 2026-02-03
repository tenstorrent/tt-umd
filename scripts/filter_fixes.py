#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import sys
from pathlib import Path

try:
    import yaml  # PyYAML
except ImportError:
    print(
        "ERROR: PyYAML not installed. Try: python3 -m pip install pyyaml",
        file=sys.stderr,
    )
    sys.exit(2)


def matches(name: str, allow: list[str]) -> bool:
    # allow items can be exact or prefix like "modernize-"
    for a in allow:
        if a.endswith("*"):
            if name.startswith(a[:-1]):
                return True
        elif name == a:
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument(
        "--allow",
        required=True,
        help="Comma-separated list. Use * for prefix (e.g. modernize-*)",
    )
    args = ap.parse_args()

    allow = [x.strip() for x in args.allow.split(",") if x.strip()]
    in_dir = Path(args.in_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    kept_files = 0
    kept_diags_total = 0
    processed = 0

    for p in sorted(in_dir.glob("*.yaml")):
        processed += 1
        with p.open("r", encoding="utf-8") as f:
            doc = yaml.safe_load(f)

        # clang-tidy export-fixes typically includes Diagnostics: [...]
        diags = doc.get("Diagnostics", [])
        if not isinstance(diags, list):
            continue

        kept = [d for d in diags if matches(d.get("DiagnosticName", ""), allow)]
        if not kept:
            continue

        doc["Diagnostics"] = kept

        out_path = out_dir / p.name
        with out_path.open("w", encoding="utf-8") as f:
            # Preserve YAML document markers (--- at start, ... at end) like clang-tidy uses
            f.write("---\n")
            yaml.safe_dump(doc, f, sort_keys=False)
            f.write("...\n")

        kept_files += 1
        kept_diags_total += len(kept)

    print(f"Processed {processed} YAMLs")
    print(f"Wrote {kept_files} YAMLs with {kept_diags_total} kept diagnostics")


if __name__ == "__main__":
    main()
