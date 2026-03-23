#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
"""Check and fix C++ single-line comments to end with a period if they're complete sentences."""

import re
import sys


def main():
    files_modified = []

    for filepath in sys.argv[1:]:
        try:
            with open(filepath, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except Exception as e:
            print(f"Error reading {filepath}: {e}")
            continue

        modified = False

        for i, line in enumerate(lines):
            stripped = line.strip()

            # Check if this is a single-line comment starting with uppercase
            match = re.match(r"^//\s+([A-Z].*)$", stripped)
            if not match:
                continue

            comment_text = match.group(1).rstrip()

            # Skip copyright and license headers
            if (
                "SPDX-" in comment_text
                or "Copyright" in comment_text
                or "SPDX-" in comment_text
            ):
                continue

            # Check if next line is also a comment (continuation)
            is_continuation = False
            if i + 1 < len(lines):
                next_stripped = lines[i + 1].strip()
                if next_stripped.startswith("//"):
                    is_continuation = True

            # Skip if it's a continuation
            if is_continuation:
                continue

            # Skip if it ends with punctuation that indicates continuation
            if comment_text.endswith((":", ",", "(", "{", "[", "-", "\\")):
                continue

            # Skip if it ends with proper punctuation
            if comment_text.endswith((".", "!", "?")):
                continue

            # Add period to the comment
            lines[i] = lines[i].rstrip() + ".\n"
            modified = True
            print(f"Fixed {filepath}:{i+1}: Added period to comment")

        # Write back the file if modified
        if modified:
            try:
                with open(filepath, "w", encoding="utf-8") as f:
                    f.writelines(lines)
                files_modified.append(filepath)
            except Exception as e:
                print(f"Error writing {filepath}: {e}")

    return 1 if files_modified else 0


if __name__ == "__main__":
    sys.exit(main())
