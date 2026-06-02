# Simulation Server Process — Design Discussion

**Status:** Draft for discussion · **Issue:** [#2677](https://github.com/tenstorrent/tt-umd/issues/2677)

This document frames the problem and lays out the design decisions we need to make to run
the simulator **persistently, as a server process** — decoupled from the lifetime of any
single UMD client. It is meant to drive a design discussion, not to prescribe a final
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

This is precisely the silicon contract: the card sits in a PCIe slot, **KMD** enumerates it,
and any process can open/share it. The card's lifetime is independent of any user process.

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

1. **Invert the roles** — the simulator (or a broker in front of it) becomes the "card"; UMD
   becomes a client that connects to it.
2. **Allow many clients** — move from a single point-to-point connection to one that accepts
   multiple clients (this is also what lets us notice when a client disconnects or crashes).
3. **Move ownership out of UMD** — a client going away must no longer tear the device down.

## 3. Scope

**Both simulation backends — RTL/emu and TTSim — are targets.** The end state is that either
backend can run as a shared, persistent server. They differ only in how much work it takes to
get there:

- **RTL/emu** already runs out-of-process over a socket, so the server is a direct extension
  of what exists today — the smaller lift.
- **TTSim** runs inside the UMD process today, so making it a server means introducing a
  process boundary where there is none. This carries a performance cost (in-process calls
  become cross-process) — a larger lift, but in scope, not optional.

Both backends should sit behind the **same** client-facing path so the server model is
uniform; the backend difference stays below the connection, invisible to clients.

---

## 4. Surfacing & discovery (the "KMD analog")

Surface each simulated device as a **file in a well-known folder** — one file per device,
discovered by scanning the folder. This mirrors how KMD exposes silicon devices for
enumeration.

- **Folder.** A well-known, per-user location (so devices of different users don't collide).
- **Identity.** Each file's name carries enough identity — which device/cluster it belongs to,
  its architecture — for a client to choose a device before connecting.
- **Liveness.** A file may be left behind after a server dies, so a file's *existence* does not
  prove a server is alive behind it. A client confirms liveness before relying on a device,
  and a stale leftover can be safely reclaimed. (This is the equivalent of "is the card really
  there?" — a filename alone can't guarantee it, the way an enumerated silicon device does.)
- **Discovery component.** We need a **`SimulationTopologyDiscovery`** that scans the folder,
  determines which devices are live, and produces the discovered devices and the cluster
  topology — mirroring how silicon discovery works — so the rest of cluster setup is unchanged.

---

## 5. The client/UMD side

In the server model the client path is **uniform: UMD always attaches to a server.** There is
no per-call "spawn vs connect" mode — opening a simulated device always means connecting to a
server and attaching, the same single path for both backends. This keeps the client close to
the silicon device-open path, which likewise just *opens* an already-present device.

- **Attach ≠ initialize.** Today initialization happens as part of launching the simulator. In
  the server model, one-time init and power-on reset happen **once, at card creation** — never
  per client. A client only ever performs a **non-mutating** attach: opening the device issues
  no state-changing operations ("opening an already-up card doesn't reset it").
- **Server is the single source of truth for topology/identity.** On attach the client *reads*
  the device's identity and cluster topology from the server rather than synthesizing its own —
  mandatory for handoff, so every client sees an identical device shape.
- **Legacy spawn-and-own stays separate.** Today's coupled behavior (UMD launches and owns the
  simulator directly, one-to-one) remains available unchanged for back-compat; it is not
  folded into the attaching client (see §6.1).

---

## 6. Proposed solution & architecture

The simulator runs as a **persistent server process that *is* the device** ("the card"): it
owns all device state and outlives any individual UMD client. UMD instances are clients that
**attach** to it, do work, and **detach** — exactly the relationship a process has with a
silicon card sitting in a PCIe slot. This section covers how a client connects; §7 covers the
server's lifecycle (starting, stopping, clean slate) and §8 the API.

### 6.1 How UMD connects to the server

Connecting should mirror how UMD opens a silicon device:

- **Discovery.** Devices are surfaced in a well-known folder (§4) — the analog of how KMD
  exposes silicon cards for discovery. UMD scans that folder to enumerate available simulated
  devices, honoring the same device-selection semantics it already uses for silicon.
- **Open = attach.** UMD picks a device and attaches to it. The attach is a **non-mutating**
  handshake: UMD learns the device's identity and topology *from the server* and registers
  itself — it does not reset or re-initialize anything.
- **Liveness.** A listed device may be stale (server gone); UMD treats a successful attach as
  the proof of liveness, the same way opening a silicon device confirms it is really there.

The net effect: from UMD's perspective "find a simulated device and open it" looks the same as
finding and opening a silicon device — which is what keeps the client path close to silicon.

---

## 7. Starting & stopping the server

The card's lifetime is **independent of any client** — that is the whole point (shareability +
state handoff). This section covers how the server comes into existence, how it goes away, and
how a client can demand a fresh one.

### 7.1 Starting the server

The server is brought up **explicitly** — there is no auto-spawn:

- **Created explicitly by an owner.** It is started out-of-band — by a person, a CI step, or a
  service — *before* any UMD process attaches, and stays up across many attach/detach cycles.
  This is the faithful silicon model (the card is already powered and enumerated before
  software touches it) and the one that cleanly supports state handoff between separate UMD
  processes. The owner is simply whoever created it.
- **Legacy mode (back-compat).** Today's behavior — UMD launches and owns the simulator
  directly (one-to-one, coupled lifetime, no server) — remains available as a transition path
  for anyone not yet on the server model.

**What creating a server takes.** Creation fully specifies *what device this is*. Everything
here is fixed **once, at creation**, and then **served to every client** on attach — clients
read it, never supply it. This is what makes the server the single source of truth and keeps
every client's view identical, which handoff depends on.

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

### 7.2 Stopping the server

Because the card's lifetime is decoupled from clients, **a client detaching or exiting must
never tear the server down** (this is the key change from today, where a client going away
kills the simulator). Stopping is its own action, exposed **in the UMD API and callable by any
client** — it is a shared capability, not restricted to the creator.

**Teardown triggers — when the card shuts down:**

- **Explicit stop** — any client asks the server to shut down, through the UMD API.
- **Policy-based** — e.g. linger for a grace period after the last client detaches, then exit;
  or stay up indefinitely for a long-lived shared card. A bounded linger keeps CI from leaking
  server processes while still surviving the brief gap during a process-to-process handoff.

**Behavior with clients still attached — to be decided.** What a stop should do when clients
are still attached (graceful drain, forceful, or refuse-if-busy) is left open for a later
discussion. Regardless of the choice, a client whose card disappears must **surface an error,
not hang** (the same contract as a silicon card that vanishes).

**Cleanup.** A graceful stop removes the device's discovery entry and tears down the backend,
leaving the folder clean. An unclean exit (crash / hard kill) leaves a stale entry, reclaimed
by the **liveness** mechanism (§4) — so the two paths are complementary.

**State.** Stopping the server **destroys all device state** — the card powers off. This is
distinct from a clean-slate *reset* (§7.3), which keeps the server running and only resets
state. Stopping a server that is already gone is a clean no-op.

**Client accounting.** The card tracks how many clients are attached, because the policy above
depends on knowing when the last one leaves. A clean exit detaches and releases its reference;
a **crashed** client never sends that detach, so the server detects the dropped connection and
releases the reference on its behalf. Crash detection is *only* bookkeeping — a crash is
treated exactly like a clean detach (release one reference, nothing more), so it neither leaks
a reference (keeping the card alive forever) nor wrongly brings the card down.

### 7.3 Resetting to a clean slate

Persisting state across attach/detach is the default (it's what enables handoff), but a client
will sometimes want a **fresh device** — power-on defaults, nothing left over from a previous
run. Because state lives in the server, "clean slate" is a deliberate, separate action, never a
side effect of attaching (this preserves the **attach ≠ initialize** rule from §5).

Two granularities, which can coexist:

- **Restart the server** — tear the card down and create a new one. The simplest, most total
  clean slate; it's just stopping (§7.2) followed by starting (§7.1).
- **Reset in place** — an explicit operation on a *running* server that re-runs the one-time
  power-on init/reset and returns device state to defaults, without dropping the process or
  forcing every client to reconnect.

The important property either way: a reset is **destructive and shared** — it wipes the state
that *all* currently attached clients are using. So it must be an explicit action (requested by
a client that knowingly opts in), and it cannot be the quiet default behavior of opening the
device.

---

## 8. The UMD ↔ server API

The API is the contract every client uses to talk to the card. Guiding principle: it should
expose the **same device operations UMD already performs against silicon**, so the client
stays a thin pass-through and the path stays close to silicon — *plus* the session verbs that
multi-client sharing requires. The capability surface:

- **Session.** Attach and detach. Attach is **non-mutating**: it registers the client and
  returns the device description (below); detach releases it. These verbs are what make
  multi-client access and reference-counting (§7.2) work.
- **Device description / identity.** A query — answered at attach — for architecture, board,
  cluster topology, and memory layout: everything a client must *read* rather than assume.
  This is what makes the server the single source of truth and keeps every client's view
  identical (required for handoff).
- **Device memory access.** Read and write device memory addressed by (endpoint, address,
  size) — core-local memory, DRAM, registers, and multicast writes. This is the bulk of the
  traffic.
- **Host memory (sysmem).** A client **allocates** a host-visible memory region through the API
  (and frees it), then reads/writes it; the server provides the backing and a device-visible
  address so the device can transfer to and from it. Allocation is a **runtime** operation, not
  fixed at creation — mirroring how host memory is pinned on demand on silicon. These regions
  are a **per-session** resource, released when the client detaches; unlike device memory they
  do not persist across handoff.
- **Run / reset control.** Assert/deassert core resets and start execution. These are normal
  operations a client may issue while attached; what's special is only that *attaching* never
  implicitly resets (§5).
- **Execution / time.** Because the clock is server-owned and free-running, advancing time is a
  server concern, not a per-client knob; any stepped/deterministic mode is a global operation.
  This group is simulation-specific and has no silicon analog.
- **Management / clean slate.** Reset-to-clean-slate (§7.3) and shutdown (§7.2) — kept distinct
  from normal per-client operations because they affect *all* attached clients.

Explicit ordering/synchronization (memory barriers) is intentionally **not** part of the API:
operations apply synchronously and in order before the response returns, so there is nothing to
flush — the simulator's memory barriers are already no-ops today.

Cross-cutting properties of the API:

- **Per-client sessions** — the server attributes every operation to a client, so it can
  reference-count and notice disconnects.
- **Per-operation atomicity, no cross-client transactions** — each operation completes
  atomically against shared state, but no client can lock the card across multiple operations.
  This matches silicon, where individual accesses are atomic but there is no cross-process
  transaction guarantee.
- **Request/response** — synchronous from the client's point of view.
- **Backend-agnostic** — identical for RTL and TTSim; the backend difference stays below the
  API.
