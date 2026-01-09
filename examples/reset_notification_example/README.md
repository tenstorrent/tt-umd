# Warm Reset Notification Example

This example set demonstrates the **Inter-Process Communication (IPC)** mechanism used to coordinate Warm Resets across the system. It shows how independent processes (listeners) can automatically pause or terminate when a reset is triggered by another process (notifier).

## Building and Running

```bash
# Configure with examples enabled
cmake -B build -DTT_UMD_BUILD_EXAMPLES=ON

# Build
cmake --build ./build

# Executables will be located at:
# ./build/examples/warm_reset/notifier_example
# ./build/examples/warm_reset/listener_example
```

## What it Demonstrates

The Tenstorrent UMD provides a mechanism using Unix Domain Sockets to broadcast reset events system-wide. This allows UMD clients to safely stop accessing the device before a reset occurs and resume afterwards.

### There are two key roles in this architecture:

- Notifier (notifier_example): The process performing the reset. It scans the listener directory, sends a PRE_RESET signal to all connected clients, waits for a timeout, performs the hardware reset, and finally sends a POST_RESET signal.

- Listener (listener_example): A client application (e.g., an AI workload, telemetry etc.) that listens for these signals to safely stop or pause its access to the device during the maintenance window.

### Usage Pattern

You will need two terminal windows to observe the coordination.

1. Start the Listener (Workload)

The listener_example simulates a running application doing work. You can start it in one of two modes to see different behaviors:

- Stop mode: The application simulates a client that cannot recover from a reset. It terminates gracefully immediately upon receiving the PRE_RESET signal.

- Pause mode: The application simulates a robust client. It goes dormant on PRE_RESET, waits for POST_RESET, and then resumes processing.

```bash
# Terminal 1

# Scenario A: Client that stops on reset
./listener_example stop

# Scenario B: Client that pauses and resumes
./listener_example pause
```

2. Trigger the Reset

Once the listener is running, use the notifier_example in a separate terminal to trigger the reset flow. You can optionally specify a timeout (in seconds) that the notifier waits for clients to cleanup before resetting.

```bash
# Terminal 2

# Trigger reset with a 2-second timeout (default)
./notifier_example

# Trigger reset with a custom 5-second timeout
./notifier_example 5
```

## Docker and Cross-Environment Compatibility

This IPC mechanism works across different environments, including Host-to-Host, Docker-to-Docker, and Host-to-Docker communication.

Requirement: Because the communication relies on Unix Domain Sockets located in /tmp/tt_umd_listeners, this directory must be mounted if running inside containers.

```bash
docker run -v /tmp/tt_umd_listeners:/tmp/tt_umd_listeners -it my_image ./listener_example pause
```

**Note: If the volume is not mounted, the Notifier (running on the host or another container) will not see the Listener, and the Listener will not receive any events.**

## Important Implementation Notes

### Signal Handling

If the listener_example (or any other process that monitors it's application for a reset mechanism) is terminated abruptly (e.g., via SIGINT / Ctrl+C) without a proper signal handler, the socket file in **/tmp/tt_umd_listeners/** may remain on disk as a "stale" file.


## Future Integration

Note: The notification flow demonstrated here (notify_pre -> reset -> notify_post) is currently explicit for demonstration purposes. In the final release, these calls will be automatically incorporated into WarmReset::warm_reset(), making the coordination implicit for any user calling the reset API.