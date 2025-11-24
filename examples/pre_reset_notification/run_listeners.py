# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

#!/usr/bin/env python3

import subprocess
import multiprocessing
import os
import signal
import sys


processes = []


def signal_handler(signum, frame):
    """Handle termination signals and kill all child processes."""
    print("\nReceived signal, terminating all listeners...")
    for proc in processes:
        if proc.poll() is None:  # Process still running
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            except ProcessLookupError:
                pass
    sys.exit(0)


def run_listener(process_id):
    """Run a single listener_example process."""
    print(f"Starting listener {process_id}")
    process = subprocess.Popen(
        ["./build/examples/pre_reset_notification/listener_example"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        preexec_fn=os.setsid  # Create new process group
    )
    processes.append(process)
    
    # Print output with PID prefix
    for line in process.stdout:
        print(f"[PID {process.pid}] {line}", end='')


def main():
    """Start 5 listener processes in parallel."""
    # Register signal handlers
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    with multiprocessing.Pool(processes=5) as pool:
        pool.map(run_listener, range(1, 6))


if __name__ == "__main__":
    main()
