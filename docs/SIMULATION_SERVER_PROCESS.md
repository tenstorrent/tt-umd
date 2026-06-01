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

---

## 6. Proposed solution & architecture

The simulator runs as a **persistent server process that *is* the device** ("the card"): it
owns all device state and outlives any individual UMD client. UMD instances are clients that
**attach** to it, do work, and **detach** — exactly the relationship a process has with a
silicon card sitting in a PCIe slot. Below we work through the four questions that decide the
shape of this: how the server is created, how UMD connects, who tears it down, and which KMD
responsibilities the server must take on to look like silicon.

### 6.1 Lifetime: how is the server created?

The card's existence must be **independent of any client's lifetime** — that is the whole
point (shareability + state handoff). The open question is *who brings it up and when*:

- **Offline (created out-of-band, recommended default).** The server is started explicitly —
  by a person, a CI step, or a service — *before* any UMD process runs, and stays up across
  many attach/detach cycles. This is the faithful silicon model (the card is already powered
  and enumerated before software touches it) and the only one that cleanly supports
  state handoff between separate UMD processes.
- **During UMD startup (lazy, opt-in).** For single-process/dev convenience, the first UMD
  client that finds no server may bring one up. This preserves today's "just run the binary"
  workflow, but the spawned server must still be **owned independently** of that client (it
  does not die when that client exits) — otherwise we are back to lifetime coupling.

The invariant under both: **creating the device (one-time power-on init/reset of state) is
distinct from a client attaching.** The card is initialized once, by whoever creates it;
every client — including the first — only attaches and never re-initializes state.

> **Decision:** offline-by-default, with lazy startup-spawn as an opt-in convenience? And in
> the lazy case, what owns the spawned server so it isn't tied to the spawning client?

### 6.2 How UMD connects to the server

Connecting should mirror how UMD opens a silicon device:

- **Discovery.** Devices are surfaced as endpoints in a well-known location (a folder of
  per-device sockets) — the analog of KMD exposing cards under `/dev/tenstorrent`. UMD
  **scans** that location to enumerate available simulated devices, honoring the same
  device-selection semantics it already uses for silicon.
- **Open = attach.** UMD picks a device and connects to its endpoint. The attach is a
  **non-mutating** handshake: UMD learns the device's identity and topology *from the server*
  and bumps a reference count — it does not reset or re-initialize anything.
- **Liveness.** A listed endpoint may be stale (server gone); UMD treats a successful
  connection as the proof of liveness, the same way opening a chardev confirms a real device.

The net effect: from UMD's perspective "find a simulated device and open it" looks the same as
"enumerate PCIe devices and open one" — which is what keeps the client path close to silicon.

> **Decision:** where does the discovery folder live, and how is a device named/selected
> within it?

### 6.3 Who kills the server?

Because the card's lifetime is decoupled from clients, **a client detaching or exiting must
never tear the server down** (this is the key change from today, where a client's destructor
kills the simulator). Teardown becomes a deliberate act of whoever *owns* the card:

- **Explicit stop** — the owner (person/CI/service that created it) shuts it down when done.
  Natural fit for the offline model.
- **Policy-based** — e.g. linger for a grace period after the last client detaches, then exit;
  or stay up indefinitely for a long-lived shared card. A bounded linger keeps CI from leaking
  server processes while still surviving the brief gap during a process-to-process handoff.
- **Crashed clients** are detected (their connection drops) and detached automatically — they
  decrement the reference count but never bring the card down.

> **Decision:** explicit-owner teardown, a linger policy, or both (different defaults for
> interactive vs CI)? What is the default linger?

### 6.4 Resetting to a clean slate

Persisting state across attach/detach is the default (it's what enables handoff), but a client
will sometimes want a **fresh device** — power-on defaults, nothing left over from a previous
run. Because state lives in the server, "clean slate" is a deliberate, separate action, never
a side effect of attaching (this preserves the **attach ≠ initialize** rule from §6.1).

Two granularities, which can coexist:

- **Restart the server** — tear the card down and create a new one. The simplest, most
  total clean slate; it's just teardown (§6.3) followed by create (§6.1).
- **Reset in place** — an explicit operation on a *running* server that re-runs the one-time
  power-on init/reset and returns device state to defaults, without dropping the process or
  forcing every client to reconnect.

The important property either way: a reset is **destructive and shared** — it wipes the state
that *all* currently attached clients are using. So it must be an explicit, privileged action
(requested by the owner, or by a client that knowingly opts in), and it cannot be the quiet
default behavior of opening the device.

> **Decision:** do we need in-place reset, or is "restart the server for a clean slate" enough?
> Who is allowed to trigger a reset, and what is the contract for other clients that are
> attached when it happens (forcibly detached, notified, or silently see reset state)?
