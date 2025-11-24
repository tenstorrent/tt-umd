#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import subprocess
import multiprocessing


def run_listener(process_id):
    """Run a single listener_example process."""
    print(f"Starting listener {process_id}")
    process = subprocess.Popen(
        ["./build/examples/pre_reset_notification/listener_example"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )
    
    # Print output with PID prefix
    for line in process.stdout:
        print(f"[PID {process.pid}] {line}", end='')


def main():
    """Start 5 listener processes in parallel."""
    with multiprocessing.Pool(processes=5) as pool:
        pool.map(run_listener, range(1, 6))


if __name__ == "__main__":
    main()
