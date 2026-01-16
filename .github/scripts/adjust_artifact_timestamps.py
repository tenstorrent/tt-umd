#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

"""
Adjust artifact timestamps based on actual build dependencies.

This script uses ninja's dependency tracking to set artifact timestamps correctly:
- For each output file: timestamp = max(input file timestamps) + 1

This ensures that when build artifacts are cached and restored later:
- If a source file is newer than the artifact, rebuild is triggered
- If a source file is older, the cached artifact is used

Usage:
    python adjust_artifact_timestamps.py <build_dir>
"""

import subprocess
import os
import sys
from pathlib import Path
from typing import Dict, Set, List


def get_dependencies(build_dir: Path, target: str) -> List[str]:
    """Get the list of input dependencies for a target."""
    deps_result = subprocess.run(
        ['ninja', '-t', 'query', target],
        capture_output=True,
        text=True,
        cwd=build_dir
    )
    
    if deps_result.returncode != 0:
        return []
    
    dependencies = []
    in_inputs_section = False
    
    for line in deps_result.stdout.splitlines():
        stripped = line.strip()
        
        # Check if we're entering the inputs section
        if stripped.startswith('input:'):
            in_inputs_section = True
            # Get any inputs on the same line after "input:"
            rest = stripped[6:].strip()  # Remove "input:"
            if rest and not rest.startswith('||'):
                dependencies.append(rest)
            continue
        
        # Check if we're leaving the inputs section
        if stripped.startswith('outputs:') or stripped.startswith('build:'):
            in_inputs_section = False
            continue
        
        # If we're in the inputs section, collect dependencies
        if in_inputs_section:
            # Skip order-only dependencies (lines starting with ||)
            if stripped.startswith('||'):
                continue
            
            # Skip empty lines
            if not stripped:
                continue
            
            # This is a dependency - add it
            dependencies.append(stripped)
    
    return dependencies


def find_timestamps(build_dir: Path) -> Dict[Path, float]:
    """
    Calculate correct timestamps for all build artifacts.
    
    Returns a dictionary mapping file paths to their computed timestamps.
    Timestamps are computed as: max(dependency timestamps) + 1
    
    For files outside the build directory (source files), use existing timestamp.
    For files inside the build directory (artifacts), compute recursively.
    """
    print(f"Computing artifact timestamps in {build_dir}")
    
    # Get all build targets from ninja
    result = subprocess.run(
        ['ninja', '-t', 'targets', 'all'],
        capture_output=True,
        text=True,
        cwd=build_dir
    )
    
    if result.returncode != 0:
        print(f"Error getting ninja targets: {result.stderr}")
        return {}
    
    targets = []
    for line in result.stdout.splitlines():
        if ':' in line:
            target = line.split(':')[0].strip()
            targets.append(target)
    
    print(f"Found {len(targets)} build targets")
    
    # Dictionary to store computed timestamps
    timestamps: Dict[Path, float] = {}
    # Cache to avoid recomputing
    computed: Set[Path] = set()
    
    def compute_timestamp(target_path: Path) -> float:
        """Recursively compute timestamp for a target."""
        # If already computed, return cached value
        if target_path in computed:
            return timestamps.get(target_path, 0)
        
        # If file doesn't exist, return 0
        if not target_path.exists():
            return 0
        
        # If file is outside build directory, use its actual timestamp
        try:
            if not target_path.is_relative_to(build_dir):
                mtime = target_path.stat().st_mtime
                timestamps[target_path] = mtime
                computed.add(target_path)
                return mtime
        except (ValueError, OSError):
            # is_relative_to can raise ValueError, stat can raise OSError
            # Treat as source file
            try:
                mtime = target_path.stat().st_mtime
                timestamps[target_path] = mtime
                computed.add(target_path)
                return mtime
            except OSError:
                return 0
        
        # File is inside build directory - compute based on dependencies
        relative_target = str(target_path.relative_to(build_dir))
        dependencies = get_dependencies(build_dir, relative_target)
        
        max_dep_time = 0
        for dep in dependencies:
            dep_path = build_dir / dep
            dep_time = compute_timestamp(dep_path)
            max_dep_time = max(max_dep_time, dep_time)
        
        # Set timestamp = max(dependencies) + 1
        new_timestamp = max_dep_time + 1 if max_dep_time > 0 else 0
        timestamps[target_path] = new_timestamp
        computed.add(target_path)
        return new_timestamp
    
    # Compute timestamps for all targets
    for target in targets:
        target_path = build_dir / target
        compute_timestamp(target_path)
    
    # Filter to only return paths inside build directory that need updating
    result_timestamps = {
        path: ts for path, ts in timestamps.items()
        if path.exists() and path.is_relative_to(build_dir) and ts > 0
    }
    
    print(f"Computed timestamps for {len(result_timestamps)} artifacts")
    return result_timestamps


def update_timestamps(timestamps: Dict[Path, float]) -> None:
    """Apply the computed timestamps to files."""
    print(f"Applying timestamps to {len(timestamps)} files...")

    adjusted_count = 0
    for path, timestamp in timestamps.items():
        try:
            previous_timestamp = path.stat().st_mtime
            os.utime(path, (timestamp, timestamp))
            if previous_timestamp != timestamp:
                adjusted_count += 1
                print(f"  Set {path.name} to timestamp {timestamp} from {previous_timestamp}")
        except OSError as e:
            print(f"Warning: Could not adjust timestamp for {path}: {e}")

    print(f"Timestamp adjustment complete. Adjusted {adjusted_count} files.")


def main():
    if len(sys.argv) != 2:
        print("Usage: adjust_artifact_timestamps.py <build_dir>")
        sys.exit(1)
    
    build_dir = Path(sys.argv[1]).resolve()
    
    if not build_dir.exists():
        print(f"Error: Build directory {build_dir} does not exist")
        sys.exit(1)
    
    if not (build_dir / 'build.ninja').exists():
        print(f"Error: {build_dir} does not appear to be a ninja build directory")
        sys.exit(1)
    
    # Two-phase approach:
    # 1. Compute all timestamps recursively, handling build directory dependencies
    # 2. Apply the computed timestamps
    timestamps = find_timestamps(build_dir)
    update_timestamps(timestamps)


if __name__ == '__main__':
    main()
