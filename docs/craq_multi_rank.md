# CRAQ same-host multi-rank simulation

The direct `libttsim.so` backend supports local multi-chip routing separately
from cross-process Ethernet. Cross-process links require a simulator advertising
bit 0 of `libttsim_eth_fd_capabilities` and exporting checked
`libttsim_attach_eth_link_fd` / `libttsim_detach_eth_link_fd`. Libraries without
these entry points still support their existing local simulation modes. Current
CRAQ implements this transport for Wormhole and Blackhole, not Quasar.

## Launch contract

Use one freshly created directory per job, owned by the launching user with mode
0700. Set `TT_SIM_ETH_IPC_DIR` to the same absolute path in every participating
rank. For example, a launcher can create it with `mktemp -d`, export its path,
start all ranks concurrently, wait for all of them to exit, then remove the
directory. Use a trusted parent directory. Do not put it in the simulation-server
socket allocator's namespace: that server's cleanup treats socket-free
directories as stale.

The transport is same-host and same-user. Every rank must receive the same
global topology before constraining it to its visible devices. The descriptor
must contain authentic, unique `chip_unique_ids`; synthetic rank-local IDs are
rejected. Remote links must name the reciprocal global chip ID and Ethernet
channel. A local channel cannot also have a local-process peer.

Each receiver exclusively creates its receive FIFO, mode 0600. Existing receive
paths are errors, including FIFOs left by crashed jobs; use a fresh job directory
on restart. Writers validate that the peer path is a private FIFO. Symlinks and
regular files are rejected. All receive descriptors open before waiting for
writers. Setup uses a single 120-second monotonic deadline across connections,
readiness exchange, and MAC registry lock waits, with the awaited endpoint in
errors. A required missing peer fails cluster construction.

After all outgoing writers connect, each rank writes an eight-byte `TTSIMFD1`
record on every outgoing FIFO, then consumes exactly that record from every
incoming FIFO. Only then may the simulator attach and exchange Ethernet frames.
The handshake distinguishes initial lack of a writer from later peer loss.
Neither rank may start simulator traffic on a link before finishing its own
handshake. Use the same protocol version on all ranks.

MACs derive deterministically from global chip ID and channel, using a locally
administered unicast prefix separate from in-process endpoints. The session
registry stores the complete identity and rejects hash collisions. Reservations
remain until launcher cleanup so crashes cannot silently reassign identities.
Dedicated FIFO routing does not depend on MAC lookup; firmware boot still needs
qualification for the intended workload.

## Ownership and failure

UMD owns descriptors. CRAQ borrows them after checked attachment succeeds and
must not close them. UMD detaches each endpoint before closing its descriptors
and unlinking its receive FIFO, even if another communicator keeps the simulator
library alive. Construction failures unwind pending and attached links. The
launcher owns the directory and removes it only after all ranks have stopped.

The transport stays nonblocking: CRAQ retains short-write progress and retries
when the pipe fills. Peer EOF, polling errors, and broken pipes are reported as
transport failures rather than interpreted as idle links. The readiness write
masks SIGPIPE only on the calling thread and preserves an already pending signal;
it does not change the application's process-wide signal handler.

## Validation

`EthIpcTest` tests exercise isolated sessions, startup ordering, invalid
paths, bounded missing-peer setup, descriptor cleanup, and readiness failures.
Simulator transport tests additionally cover checked attachment/detachment,
partial writes, saturated pipes, and peer disappearance. End-to-end qualification
must use both repositories' matching revisions and the actual consumer topology;
host tests alone do not establish firmware or application correctness.
