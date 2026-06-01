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

- **RTL/emu is the primary target** — already out-of-process and socket-based, so the server
  is an extension of the existing `SimulationHost`/nng/FlatBuffers stack (1:1 → N:1).
- **TTSim** can be offered later as an opt-in server mode reusing the same client device
  class, but pays an IPC penalty (in-process calls → cross-process) and carries a TLB hazard
  (§6.3). **Proposal: ship RTL-only first; keep TTSim single-client/in-process initially.**

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

## 6. The hard problems (need explicit decisions)

### 6.1 Sysmem / host-memory DMA with N clients — *the crux*

Host sysmem is a **UMD-process-local anonymous `mmap`** today (`SimulationSysmemManager`), and
simulator-initiated DMA (`AXI_RAM_*` notifications) is serviced against *that one client's*
address space. With N clients, the shared device cannot answer **"whose sysmem?"**

- **Recommended — sysmem becomes server-resident shared state.** The PCIe/sysmem address
  space is a resource of the card (like L1/DRAM). Device DMA resolves *entirely inside the
  server* — no client round-trip, no "which client," no reentrancy deadlock — and it enables
  handoff. Silicon-faithful (sysmem = pinned, IOMMU-mapped host DRAM).
  - Cost: client `read/write_to_sysmem` becomes an IPC round-trip (loses today's zero-copy).
  - Refinement to prototype: `shm_open`/`memfd` backing mapped by *both* server and client to
    recover zero-copy while keeping the server authoritative.
  - Needs server-arbitrated channel/PCIe-base allocation (the KMD-allocates-IOVA analog).
- **Rejected — keep sysmem client-local + route DMA to the owning client.** Requires the sim
  core to resolve client identity from a bus address (deep sim change), reintroduces a
  callback-reentrancy deadlock, and breaks handoff (in-flight DMA to a dead client).

> **Decision:** server-resident sysmem — proxy-everything (simple/correct/slower) vs `shm`
> zero-copy fast path?

### 6.2 Clock ownership

Device time is global; a client calling `advance_clock` / `libttsim_clock` affects all
clients.

- **Recommended — the server owns a free-running clock by default.** The card runs
  continuously; clients observe asynchronously (the only model where tt-triage peeking at a
  *running* workload makes sense). Expose `CLOCK_ADVANCE`/`PAUSE` only as a privileged/global
  op.
- **Risk:** existing single-client flows likely depend on lock-step `write → advance N →
  read` determinism. Provide a **single-client stepped/deterministic mode** (when exactly one
  client is attached, or it holds an advertised "clock-owner" lease); the contract degrades to
  free-running the moment a second client attaches.

> **Decision:** default to free-running? **Action:** audit existing sim tests for hidden
> dependence on synchronous `advance_clock`.

### 6.3 TTSim TLB registers (TTSim-only)

On **TTSim WH/BH**, `TTSimTlbHandle::configure` programs *authoritative device TLB registers*
via BAR0 with a reprogram-per-access pattern that is only safe under the in-process
`device_lock_`. Two concurrent clients would stomp each other. **Fix:** server owns TLB
allocation, or per-access reprogram+access becomes one atomic server-side command. (RTL is
unaffected — its TLB config is pure client-side address arithmetic, never latched in the
device.)

### 6.4 Concurrency semantics

**Recommended — match silicon: per-command atomicity, no multi-command/cross-client
transactions.** A single server-side command loop (one device-owner thread draining a queue,
fed by per-client readers) gives this for free and avoids the DMA-callback reentrancy
deadlock a lock-based design would hit. The per-process `device_lock_` is **not** sufficient
cross-client — serialization moves server-side. Don't offer client-held device-wide locks
(diverges from silicon; lets one client wedge a shared card).

---

## 7. Lifetime & ownership

- **Who starts it:** a standalone `tt-sim-server` daemon (= the card), fronted by a thin
  `tt-sim-launcher` that does race-free create-or-attach under a per-name `flock`, waits for
  socket readiness, and cleans up stale tombstones. Keep **lazy auto-spawn opt-in**
  (`TT_SIM_AUTOSPAWN=1`) so today's one-binary dev/CI workflow survives migration. systemd
  socket-activation is the most KMD-like option and worth documenting for a shared lab sim,
  but can't be the baseline (environment constraints).
- **Who reaps it:** **connection-death-driven refcount + configurable linger.** Default
  `linger=infinite` for the daemon/handoff path; bounded linger (~30–60s) for autospawn/CI so
  RTL processes don't leak while still surviving test-to-test handoff. **Reject** instant
  refcount-to-zero kill (breaks handoff, races). Crashed clients: connection-close = implicit
  detach (primary signal); heartbeat/lease as backstop for half-open connections.
- **Orphans:** the server unlinks its own socket+PID on clean exit; tombstones (hard kill) are
  reclaimed only by the launcher under the folder lock.

> **Decisions:** daemon + launcher vs lazy-spawn default; teardown policy & default linger;
> who is responsible for the device's initial reset/power state on creation.

---

## 8. Protocol changes (`simulation_device.fbs`)

- **Add:** `ATTACH`/`ATTACH_REPLY` (versioned handshake + device facts + serialized
  `ClusterDescriptor` + server-assigned client id), `DETACH`, `GET_DEVICE_INFO`,
  `SYSMEM_READ`/`SYSMEM_WRITE` (§6.1), `BARRIER`/flush-ack (membar = "wait for my outstanding
  write acks"), privileged `CLOCK_ADVANCE`/`PAUSE`, a generic `ERROR`/status field, a
  per-client monotonic `request_id`.
- **Remove:** the per-client `START` and the `EXIT`-as-ready-ack hack; `EXIT`/destroy becomes
  launcher/SIGTERM-only.
- **Versioning is mandatory.** There is already an in-tree op-code **tag collision**
  (`simulation_device.fbs:14-17`, AXI notifications vs NEO_DM resets) to resolve, plus
  **cross-repo coordination** with the simulator release — `run.sh` must implement the
  listener/server side.

---

## 9. State ownership audit (what breaks handoff if left client-side)

**Must move to the server (authoritative, mutable):** simulator core (already, RTL); host
sysmem backing + RAM callbacks (§6.1); TTSim TLB programming/allocation (§6.3); cross-process
serialization (replaces in-process `device_lock_`); reset/power state ownership.

**Re-fetch on attach (read-only — query, never set):** arch, SocDescriptor (validate, don't
impose), BAR0/BAR4 bases, `tlb_region_size_`, `libttsim_pci_device_id`, num host channels,
current reset/power state.

**Keep client-local (connection bookkeeping):** notification thread + command queue, the
(now-dialing) socket, RTL TLB allocator/window/config, per-client lock, selected NOC id
(carried per-command — verify the wire protocol carries it per op).

---

## 10. Proposed phasing

1. **Protocol + role inversion + transport.** Unix socket in a folder; `pair1` → per-client
   connections; split `RtlSimCommunicator::initialize()` into spawn-side vs dial-side; add
   `ATTACH`/`DETACH`; stop sending `EXIT` on client teardown.
   *Interim de-risking option:* a **UMD-side broker** (UMD owns a server process; sim stays a
   1:1 dialer behind it) delivers N:1 with **zero simulator changes**, then later collapses
   into the sim.
2. **Server-resident sysmem (§6.1)** — the correctness gate for real handoff.
3. **Discovery + Cluster integration** — `SimulationTopologyDiscovery`, `ClusterOptions`
   attach fields, `SimulationClientTTDevice`, server-served `ClusterDescriptor`.
4. **Lifetime tooling** — `tt-sim-server` + `tt-sim-launcher`, refcount/linger, stale cleanup.
5. **Clock policy (§6.2)** — free-running default + single-client stepped lease; audit tests.
6. **TTSim-as-server (optional, later)** — TLB ownership fix (§6.3) + IPC under TTSimCommunicator.

---

## 11. Decisions to make in this meeting

0. Do we accept the **role inversion** (simulator = server/owner, UMD = client)? (§2)
1. **Sysmem:** server-resident — proxy-everything vs `shm` zero-copy fast path? (§6.1)
2. **Clock:** default free-running? Who audits existing tests for `advance_clock` dependence? (§6.2)
3. **Lifetime:** explicit daemon+launcher vs lazy-spawn default; teardown policy & default
   linger; who owns the device's initial reset/power state. (§7)
4. **Transport:** unix-socket-only, or keep TCP for remote sim? Drop nng for raw sockets? (§4, §2)
5. **Scope/order:** RTL-only first with TTSim deferred? Build the **broker** interim first? (§3, §10)
6. **Cross-repo:** who owns the simulator-side (`run.sh`) server implementation and the
   protocol version contract? (§8)

## Open questions (lower priority / follow-up)

- Multi-user folder permissions — is cross-user attach ever wanted? (per-UID folder forbids
  it by default).
- Sim crash mid-handoff = state lost (no silicon equivalent). Snapshot/checkpoint desirable,
  or accept "restart fresh" for v1?
- Heartbeat/lease TTL vs RTL command latency (avoid false-positive reaping of a busy client).
