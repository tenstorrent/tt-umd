# Simulation Server Process — Design Discussion

**Status:** Draft for discussion · **Issue:** [#2677](https://github.com/tenstorrent/tt-umd/issues/2677)

This document frames the problem and lays out the design decisions we need to make to run
the simulator **persistently, as a server process** — decoupled from the lifetime of any
single UMD client. It is meant to drive a design discussion, not to prescribe a final
implementation. Each major section ends with the **decision(s)** the room needs to settle.

---

## 1. Why

Today a simulated device lives and dies with the UMD process that created it. We want it to
behave like silicon instead:

- **Shareable across processes.** Multiple UMD threads/processes attach to the *same*
  simulated device concurrently. Motivating case: **tt-triage** attaching to inspect a
  device that a workload is already running on.
- **Persistent state across clients (handoff).** One UMD client runs, mutates device state
  (L1, DRAM, registers, reset state…), and exits; a **later, separate** UMD process attaches
  and continues from exactly that state. The device is the source of truth; clients are
  ephemeral.

This is precisely the silicon contract: the card sits in a PCIe slot, **KMD** enumerates it,
and any process can open/share it. The card's lifetime is independent of any user process.

## 2. Where we are today

UMD has two simulator backends, both behind the same stack
(`Cluster → SimulationChip → TTDevice → communicator`):

| | **TTSim** | **RTL / emu** |
|---|---|---|
| Locality | in-process (`libttsim.so` via `dlopen`) | out-of-process (UMD spawns `{sim_dir}/run.sh`) |
| Transport | direct function-pointer calls | **nng** socket (TCP) + **FlatBuffers** protocol |
| Multi-client | n/a | none — see below |

**The key reality check (verified in code):** the current RTL model is *inverted* from a
"card."

- **UMD is the nng listener (server); the simulator dials *into* UMD.**
  (`SimulationHost::init()` listens; the spawned `run.sh` connects back via a random TCP port
  advertised in `NNG_SOCKET_ADDR`.)
- **The transport is `nng_pair1` — strictly 1:1.** It cannot serve N clients.
- **Lifetime is hard-coupled:** `~RtlSimulationTTDevice` → `communicator_->shutdown()` sends
  `DEVICE_COMMAND_EXIT`, which kills the simulator.

So "make it a server" is really **three coupled changes**, none optional:

1. **Invert the roles** — the simulator (or a broker in front of it) becomes the
   listener/"card"; UMD becomes a dialing client.
2. **Replace `pair1`** with per-client accepted connections (this single change underwrites
   fan-out, per-connection response ordering, *and* crash detection).
3. **Move process ownership out of UMD** — client teardown must send `DETACH`, not `EXIT`.

> **Discussion anchor:** everything below assumes we accept this inversion. If we don't, the
> rest doesn't hold together. **Decision 0: do we agree the simulator process becomes the
> server/owner, and UMD becomes a client?**

## 3. Scope

**Both simulation backends — RTL/emu and TTSim — are targets.** The end state is that either
backend can run as a shared, persistent server. They differ only in how much work it takes to
get there:

- **RTL/emu** is already out-of-process and socket-based, so the server is a direct extension
  of the existing `SimulationHost`/nng/FlatBuffers stack (1:1 → N:1) — the smaller lift.
- **TTSim** is in-process today (`libttsim.so` via `dlopen`), so making it a server means
  introducing an IPC boundary underneath `TTSimCommunicator`. This carries a perf penalty
  (in-process calls → cross-process) and requires solving the TLB-register hazard (§6.3) — a
  larger lift, but in scope, not optional.

Both backends should sit behind the **same** client device class and protocol so the server
model is uniform; the backend difference stays below the socket, invisible to clients.

---

## 4. Surfacing & discovery (the "KMD analog")

Surface each simulated chip as a **unix domain socket file in a folder** — discovered by
scanning the folder, mirroring `PCIDevice::enumerate_devices()` scanning `/dev/tenstorrent/`.

- **Folder:** default `$XDG_RUNTIME_DIR/tt-sim/` (fallback `/tmp/tt-sim-$UID/`, mode `0700`),
  env/option overridable. Per-UID namespacing gives permissions for free.
- **Filename** encodes identity for cheap pre-attach filtering (advisory cache; the socket
  handshake is authoritative): a `server_instance` id grouping one cluster's chips, plus
  arch / board / chip-NN. e.g. `tt-sim-<instance>-wormhole_b0-n300-chip00.sock`.
- **Liveness:** never trust file existence/mtime. Authoritative check = non-blocking
  `connect()` (`ECONNREFUSED` ⇒ stale tombstone) + a `flock` sentinel for race-free reclaim;
  a PID/boot-id file is a secondary diagnostic hint (guards against recycled PIDs).
- **Integration:** add a `SimulationTopologyDiscovery` that scans the folder and queries each
  socket, returning `(ClusterDescriptor, tt_devices)` — exactly parallel to silicon's
  `TopologyDiscovery::discover()`. The existing `tt_devices` reuse path in `Cluster::Cluster`
  then works unchanged.

> **Decisions:** folder location & whether it's configurable; filename grammar; whether we
> also keep TCP (remote/distributed sim) alongside unix sockets.

---

## 5. The client/UMD side

Keep the client path **as close to the silicon `LocalChip` path as possible.**

- **One device class over two communicator backings** (the silicon pattern: `LocalChip` over
  different PciDevice/JTAG backings). Inject a *connecting* communicator
  (`RtlSimClientCommunicator`) vs the spawning one. **Avoid** a `bool connect_mode` flag
  littering the constructor.
- **Keep `ChipType::SIMULATION`.** Attach-vs-spawn is a transport *mode*, not a new device
  family — a new ChipType would fork every `== SIMULATION` check in `cluster.cpp`.
- **Attach ≠ initialize.** Today *all* init is pulled into TTDevice **construction** +
  spawn/`start_sim`; `SimulationChip::start_device()` is already a no-op (conveniently correct
  for attach). Split it explicitly:
  - `INIT`/`START` + power-on resets run **once, at card creation** — never per client.
  - New read-only `ATTACH`/`HELLO` (returns arch, SocDescriptor, BAR bases, channel count,
    reset/power state, serialized `ClusterDescriptor`; bumps a refcount) and `DETACH`
    (decrements; never tears down). Replaces the current `EXIT`-as-ready-ack hack.
  - **Hard rule:** a default `Cluster(SIMULATION attach)` + `start_device()` must issue
    **zero state-mutating commands.** "Opening an already-up card doesn't reset it."
- **Server is the single source of truth for topology/identity.** Clients *read* the
  `ClusterDescriptor` from the server (reuse `ClusterDescriptor::serialize()` /
  `create_from_yaml_content()`) instead of synthesizing it via `create_mock_cluster`. This is
  mandatory for handoff — every client must see identical device shape.

> **Decisions:** new `SimulationClientTTDevice` vs refactor `RtlSimulationTTDevice`;
> `ClusterOptions` attach fields (socket folder / explicit socket / `attach_to_server` flag).
