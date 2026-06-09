# Simulation Server Process — Design Discussion

**Status:** Draft for discussion · **Issue:** [#2677](https://github.com/tenstorrent/tt-umd/issues/2677)

This document frames the problem and lays out the design decisions we need to make to run
the simulator as a **shared, hosted device** — one UMD process (the **host**) owns the
simulator and serves other UMD clients, so the device is decoupled from the lifetime of any
single *non-owner* client. It is meant to drive a design discussion, not to prescribe a final
implementation.

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

This is the silicon contract: the card sits in a PCIe slot, **KMD** enumerates it, and any
process can open/share it. We approximate it with a **host UMD process** that owns the
simulator and serves other UMD clients over a local socket. The device's lifetime is therefore
tied to that **host** process — but independent of every *non-owner* client: clients attach,
share, and hand off freely while the host stays up. (Full silicon-faithful, host-independent
lifetime — surviving the host's exit — is explicitly out of scope for now; see §6 and §7.)

## 2. Where we are today

UMD has two simulator backends:

| | **TTSim** | **RTL / emu** |
|---|---|---|
| Locality | runs **inside** the UMD process | runs as a **separate process** that UMD launches |
| Transport | direct in-process calls | a socket connection with a message protocol |
| Multi-client | not applicable | not supported — see below |

**The key reality check:** the current out-of-process (RTL) model is *inverted* from a "card."

- **UMD acts as the server and the simulator connects back into UMD** — the opposite of a card
  that exists on its own and is opened by software.
- **The connection is strictly point-to-point** — it can serve exactly one client.
- **Lifetime is hard-coupled:** the simulator is shut down when the owning UMD client goes
  away.

So "make it a server" is really **three coupled changes**, none optional:

1. **Invert the roles** — one UMD process (the **host**) owns the simulator and *is* the "card";
   other UMD processes become clients that connect to it.
2. **Allow many clients** — move from a single point-to-point connection to one that accepts
   multiple clients (this is also what lets us notice when a client disconnects or crashes).
3. **Decouple the device from non-owner clients** — a *non-owner* client going away must no
   longer tear the device down; only the host's exit ends the device.

## 3. Scope

**Both simulation backends — RTL/emu and TTSim — are targets.** The end state is that either
backend can be hosted by a UMD process and shared with other clients. The new work — a
**client-facing socket API** in front of the device — is the **same for both backends**; the
backend stays exactly where it sits today *relative to the host UMD process*:

- **RTL/emu** already runs as a separate process the host UMD launches and talks to over a
  socket; that stays an internal detail of the host.
- **TTSim** runs in-process; under this model it **stays in-process to the host** — we do *not*
  introduce a new boundary inside it. The host keeps its native fast path; only **remote
  clients** reach the device cross-process, over the client-facing socket.

So the performance cost (in-process calls becoming cross-process) is paid by **remote clients**,
for either backend, and never by the host for its own access. Both backends sit behind the
**same** client-facing path so the model is uniform; the backend difference stays below the
connection, invisible to clients.

**Initial scope (first iteration).** We build this incrementally. The first iteration targets a
**single server at a fixed socket location**: opening either attaches to the host already at
that path or, if none is live, creates it and becomes the host (**open-or-attach**). The
connect-probe at the fixed path *is* the create-vs-attach decision; liveness is simply whether
that socket is connectable, and a stale socket is reclaimed (refused → unlink → bind). Surfacing
still happens — the host binds a socket — but at a fixed path, so this **defers multi-device
discovery (§4)**: no folder scan, no filename-encoded identity, no enumeration, because the
location is known and singular. Multi-device discovery and richer device selection (§4) are
follow-on work.

---

## 4. Surfacing & discovery (the "KMD analog")

> **Future work — out of scope for the first iteration**, which uses a single server at a fixed
> socket location with no discovery (see *Initial scope* in §3). This section records the
> eventual multi-device model.

Surface each simulated device as a **file in a well-known folder** — one file per device,
discovered by scanning the folder. This mirrors how KMD exposes silicon devices for
enumeration.

- **Folder.** A well-known, per-user location (so devices of different users don't collide).
- **Identity.** Each file's name carries enough identity — which device/cluster it belongs to,
  its architecture — for a client to choose a device before connecting.
- **Liveness.** A file may be left behind after a host dies, so a file's *existence* does not
  prove a host is alive behind it. A client confirms liveness before relying on a device,
  and a stale leftover can be safely reclaimed. (This is the equivalent of "is the card really
  there?" — a filename alone can't guarantee it, the way an enumerated silicon device does.)
- **Discovery component.** We need a **`SimulationTopologyDiscovery`** that scans the folder,
  determines which devices are live, and produces the discovered devices and the cluster
  topology — mirroring how silicon discovery works — so the rest of cluster setup is unchanged.

---

## 5. The client/UMD side

Opening a simulated device is a **single, uniform call** for the consumer. Under the hood it
resolves to one of two roles, decided by whether a **live host** already exists for that device
(§4 liveness):

- **No live host → this process becomes the host.** It creates the device, runs one-time init,
  and starts serving the socket API — *and* then uses the device normally, like any client.
- **Live host exists → this process attaches as a client** over the socket.

This keeps the client close to the silicon device-open path, which likewise just *opens* an
already-present device — the create-vs-attach decision is invisible to the consumer.

- **Open modes.** Most callers want **open-or-create** (above). A triage-style caller wants
  **attach-only** — fail if no live host exists — so it inspects a running device and never
  accidentally spawns one.
- **Attach ≠ initialize (for clients).** One-time init and power-on reset happen **once**, when
  the **host** creates the device — never on a client attach. A client only ever performs a
  **non-mutating** attach: it issues no state-changing operations ("opening an already-up card
  doesn't reset it"). The host is the sole initializer.
- **Host is the single source of truth for topology/identity.** On attach a client *reads* the
  device's identity and cluster topology from the host rather than synthesizing its own —
  mandatory for handoff, so every client sees an identical device shape.
- **Non-serving (legacy) mode.** A process can also open the device purely locally — no socket,
  serving nobody — which is behaviorally today's one-to-one path. This is just the host model
  with no clients, not a separate architecture; kept for back-compat and the simplest runs.

---

## 6. Proposed solution & architecture

One **host UMD process** owns the simulator and *is* the device ("the card"): it holds all
device state, runs the backend, and serves other UMD processes over a **UNIX domain socket**.
Crucially, the host reaches the device **directly, in-process** — calling the backend (e.g. the
TTSim `.so`) with no socket and no serialization — so the owning process keeps the fastest
possible path; the socket exists **only** for remote clients, who pay the cross-process cost.
There is no separate broker or bespoke server binary — the host is an ordinary UMD process that,
on opening a device for which no live host exists, also starts serving. A host launched purely
to serve (by a person, a CI step, or a service) — doing no workload of its own — *is* a
"dedicated simulator server," via the same code path. Other UMD processes are clients that
**attach**, do work, and **detach**. The device's lifetime is tied to the host process (§7);
cross-host persistence is out of scope for now. This section covers how a client connects; §7
covers lifecycle (starting and stopping) and §8 the API.

### 6.1 How UMD opens a device

Opening mirrors how UMD opens a silicon device, with the create-vs-attach decision (§5) made
under the hood:

- **Discovery.** Devices are surfaced in a well-known folder (§4) — the analog of how KMD
  exposes silicon cards for discovery. UMD scans that folder to enumerate available simulated
  devices, honoring the same device-selection semantics it already uses for silicon.
- **Open (create or attach).** UMD picks a device. If no live host serves it, this process
  **becomes the host** — creates the device, initializes once, and binds the UNIX socket. If a
  live host exists, UMD **attaches** to it: a **non-mutating** handshake where UMD learns the
  device's identity and topology *from the host* and registers itself, resetting nothing. A
  caller may request **attach-only** to require an existing host (triage) or **open-or-create**
  (the default).
- **Liveness.** A listed device may be stale (host gone); UMD treats a successful attach as the
  proof of liveness, the same way opening a silicon device confirms it is really there. A stale
  entry is reclaimed and, under open-or-create, replaced by a freshly hosted device.

The net effect: from UMD's perspective "find a simulated device and open it" looks the same as
finding and opening a silicon device — which is what keeps the client path close to silicon.

---

## 7. Starting & stopping the host

The device's lifetime is tied to the **host** process — independent of every *non-owner*
client, but not of the host (shareability holds always; handoff holds while the host is up).
This section covers how the host comes into existence and how it goes away.

### 7.1 Starting the host

There is no separate spawn step: the host comes into existence as the **first open-or-create**
of a device for which no live host exists (§5, §6.1).

- **The first opener becomes the host.** Whoever opens a device that has no live host creates
  it, initializes it once, binds the UNIX socket, and serves from then on. The "owner" is simply
  that process. A host stays up across many attach/detach cycles by *non-owner* clients.
- **A dedicated server is just a host with no workload.** Launching a UMD process — by a
  person, a CI step, or a service — whose only job is to open-or-create the device and idle
  gives you a long-lived shared "card," through the exact same path. This is the closest
  analog to silicon (powered and enumerated before software touches it).
- **Non-serving runs.** A process may also open the device without serving (no socket) — today's
  one-to-one behavior — for back-compat and the simplest runs (§5).

**What creating the device takes.** The creating (host) process fully specifies *what device
this is*. Everything here is fixed **once, at creation**, and then **served to every client** on
attach — clients read it, never supply it. This is what makes the host the single source of
truth and keeps every client's view identical, which handoff depends on.

- **Which simulator to run** — the backend (RTL/emu or TTSim) and the specific simulator build
  to bring up. *(Open: whether this is part of the create request itself or a property of the
  launching environment.)*
- **The device shape** — architecture and board, the SoC descriptor (core layout / memory
  map), and the cluster topology (how many chips, their interconnect, harvesting). This can be
  given **minimally** (architecture + chip count, the rest defaulted) or **explicitly** (a full
  descriptor for a custom cluster).
- **Where it is surfaced** — the discovery folder (§4) the device is published into, so clients
  can find it.

Host memory (sysmem) is deliberately *not* a creation parameter — clients allocate it at
runtime through the API (§8).

**Identity** is **minted by the simulator** at creation (not supplied by the creator) and
reported to clients on attach.

**Readiness.** Creation means: bring up the backend, run the one-time power-on init/reset,
publish the device's discovery entry under its minted identity, and only then begin accepting
attaches — a client never sees a half-initialized card. This is the one-time init that the
**attach ≠ initialize** rule (§5) keeps separate from every client's attach.

### 7.2 Stopping the host

The key change from today: **a non-owner client detaching or exiting must never tear the device
down** (today a client going away kills the simulator). The device goes away only when the
**host** does.

**Teardown triggers — when the card shuts down:**

- **Host exit** — the host process exiting (its own workload finishes) or crashing tears the
  device down. This is the primary teardown path, and the source of the lifetime coupling: no
  ownership transfer, so when the host is gone the device is gone.
- **Explicit stop** — a shutdown request through the UMD API. *(Open: because the host may be
  running its own workload, whether a non-owner client may force the host to stop — versus the
  host being the only one that decides — is to be decided.)*
- **Policy-based** — for a serving host with no workload of its own, e.g. linger for a grace
  period after the last client detaches, then exit; or stay up indefinitely. A bounded linger
  keeps CI from leaking host processes while still surviving the brief gap during a
  process-to-process handoff.

**Behavior with clients still attached — to be decided.** What a stop should do when clients
are still attached (graceful drain, forceful, or refuse-if-busy) is left open for a later
discussion. Regardless of the choice, a client whose card disappears must **surface an error,
not hang** (the same contract as a silicon card that vanishes).

**Cleanup.** A graceful host exit removes the device's discovery entry and tears down the
backend, leaving the folder clean. An unclean exit (crash / hard kill) leaves a stale entry,
reclaimed by the **liveness** mechanism (§4) — so the two paths are complementary.

**State.** Host exit **destroys all device state** — the card powers off. Stopping a host that
is already gone is a clean no-op.

**Client accounting.** The host tracks how many clients are attached, because the linger policy
depends on knowing when the last one leaves. A clean detach releases the client's reference; a
**crashed** client never sends that detach, so the host detects the dropped socket connection
and releases the reference on its behalf. Crash detection is *only* bookkeeping — a crash is
treated exactly like a clean detach (release one reference, nothing more). Note this accounting
governs the linger policy, **not** the device's life: the device still dies if the *host* exits,
regardless of attached-client count.

---

## 8. The UMD ↔ host API

The API is the contract every client uses to talk to the card over the **UNIX domain socket**.
Guiding principle: it should expose the **same device operations UMD already performs against
silicon**, so the client stays a thin pass-through and the path stays close to silicon — *plus*
the session verbs that multi-client sharing requires. The capability surface:

- **Session.** Attach and detach. Attach is **non-mutating**: it registers the client and
  returns the device description (below); detach releases it. These verbs are what make
  multi-client access and reference-counting (§7.2) work.
- **Device description / identity.** A query — answered at attach — for architecture, board,
  cluster topology, and memory layout: everything a client must *read* rather than assume.
  This is what makes the host the single source of truth and keeps every client's view
  identical (required for handoff).
- **Device memory access.** Read and write device memory addressed by (endpoint, address,
  size) — core-local memory, DRAM, registers, and multicast writes. This is the bulk of the
  traffic.
- **TLB windows (NOC translation).** Allocate, configure (aim a window at an endpoint), and
  free NOC translation windows through which device access is routed. The window state lives in
  the host, but the API **exposes TLB handling** so the client manages windows the same way it
  does on silicon; windows are a per-session resource.
- **Host memory (sysmem).** A client **allocates** a host-visible memory region through the API
  (and frees it), then reads/writes it; the host provides the backing and a device-visible
  address so the device can transfer to and from it. Allocation is a **runtime** operation, not
  fixed at creation — mirroring how host memory is pinned on demand on silicon. These regions
  are a **per-session** resource, released when the client detaches; unlike device memory they
  do not persist across handoff.
- **Run / reset control.** Assert/deassert core resets and start execution. These are normal
  operations a client may issue while attached; what's special is only that *attaching* never
  implicitly resets (§5).
- **Execution / time.** Because the clock is host-owned and free-running, advancing time is a
  host concern, not a per-client knob; any stepped/deterministic mode is a global operation.
  This group is simulation-specific and has no silicon analog.
- **Management.** Shutdown (§7.2) — kept distinct from normal per-client operations because it
  affects *all* attached clients.

Explicit ordering/synchronization (memory barriers) is intentionally **not** part of the API:
operations apply synchronously and in order before the response returns, so there is nothing to
flush — the simulator's memory barriers are already no-ops today.

Cross-cutting properties of the API:

- **The host is not a client of its own API.** This API and its UNIX socket are the **remote
  path only**. The host reaches the device **directly, in-process** — calling the backend (e.g.
  the TTSim `.so`) with no socket and no serialization — so the owning process keeps the
  fastest possible path. The same backend operations are reachable two ways: directly (host)
  and over the socket (clients); the socket layer just marshals remote requests into those same
  operations.
- **Per-client sessions** — the host attributes every operation to a client, so it can
  reference-count and notice disconnects.
- **Per-operation atomicity, no cross-client transactions** — each operation completes
  atomically against shared state, but no client can lock the card across multiple operations.
  This matches silicon, where individual accesses are atomic but there is no cross-process
  transaction guarantee.
- **Request/response** — synchronous from the client's point of view.
- **Backend-agnostic** — identical for RTL and TTSim; the backend difference stays below the
  API.

---

## 9. Implementation: client/host code split

*(Unlike the sections above, this one is about code, so it names concrete components.)*

The existing simulation stack is `Cluster → SimulationChip → TTDevice (sim) → communicator →
backend`. The split introduces a **seam at the communicator**: everything *above* it — the
device API surface and stateless translation — runs unchanged in **every** process, host and
client alike. *Below* the seam the communicator has **two backings**:

- **Direct backing (host).** Calls the backend in-process — loads/calls the TTSim `.so`, or
  drives the RTL sim process — with no socket and no serialization. This is the host's own fast
  path (§6, §8).
- **Socket backing (client).** Marshals the same device operations into API requests over the
  UNIX socket to the host, correlates responses, and notices disconnects.

The host process contains **both**: the shared device API on top (for its own workload, using
the direct backing) *and* the host-only internals below (backend, device state, and the serving
layer that exposes the socket backing to remote clients). A pure client process contains only
the shared API plus the socket backing.

Litmus test — if it must survive a *non-owner* client exiting, it's a **host internal**; if it's
the consumer API or pure computation, it runs **in every process**; if it's per-connection
bookkeeping, it's **per client**.

### Host internals ("the card") — present only in the host process

- **The backend and the code that drives it** — the backend-facing half of the communicator
  (loading/calling the TTSim `.so`, or managing the RTL sim process and its socket).
- **The serving layer** — accepts client connections on the UNIX socket and dispatches their
  requests into the same backend operations the host calls directly.
- **Device state** — L1/DRAM/registers/reset state; inherent to the backend.
- **Sysmem backing** — `SimulationSysmemManager`'s memory and the device's host-memory access
  path. Allocated **per session** at the host, since sysmem is a runtime API operation (§8).
- **TLB window state** — `SimulationTlbAllocator` / TLB programming. The window state (and, for
  TTSim, the real TLB registers) lives in the host; clients drive allocation/configuration
  through the exposed TLB API (§8) rather than managing windows locally.
- **One-time init/start and power-state ownership** — runs once, when the host creates the
  device.
- **Cross-client serialization** — today's in-process device lock becomes a host concern (one
  stateful backend, the host's own access plus many clients).
- **Device-description authority** — arch, SoC descriptor, cluster topology: fixed at creation
  and reported to clients on attach (clients only read them).

### Runs in every process ("UMD")

- **`Cluster` and the `Chip`/`TTDevice` API surface** UMD consumers call. Same interface;
  underneath, the communicator uses the direct backing (host) or the socket backing (client).
- **Coordinate translation** (`CoreCoord` → NOC) and address computation — stateless; uses the
  device descriptor (host-authored, client-read) — keeps the path close to silicon.
- **The local copy of identity/topology** — authored by the host at creation; read by clients
  on attach; used for translation.

### Per client only

- **The socket backing of the communicator** — owns the session, correlates responses, and
  notices disconnects. Absent in a pure-host (no-workload) configuration that issues no device
  ops of its own.
- **Per-session sysmem handles** — the allocation lives in the host; the client holds the
  reference / device-visible address.
- **Per-session TLB window handles** — the window state is in the host; the client allocates and
  configures windows via the TLB API (§8) and holds the references.

The target shape: a remote client collapses into a **thin adapter** mapping the existing device
operations onto socket API calls, while the host runs those same operations directly — so UMD
consumers see no change either way.
