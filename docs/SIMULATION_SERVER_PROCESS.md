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
silicon card sitting in a PCIe slot. Below we work through the questions that decide the
shape of this: how the server is created, how UMD connects, who tears it down, and how a
client gets a clean slate.

### 6.1 Lifetime: how is the server created?

UMD always attaches to a server (§5); the card's existence is **independent of any client's
lifetime** — that is the whole point (shareability + state handoff). What varies is only *how
the server comes to exist* when a client wants to attach:

- **Created offline (recommended default).** The server is started out-of-band — by a person,
  a CI step, or a service — *before* any UMD process runs, and stays up across many
  attach/detach cycles. This is the faithful silicon model (the card is already powered and
  enumerated before software touches it) and the one that cleanly supports state handoff
  between separate UMD processes.
- **Spawned on demand if not active.** If a client goes to attach and finds no live server, it
  brings one up and then attaches. The spawned server must be **owned independently** of the
  client that triggered it — it does not die when that client exits, otherwise we are back to
  lifetime coupling.
- **Legacy mode (back-compat).** Today's behavior — UMD launches and owns the simulator
  directly (one-to-one, coupled lifetime, no server) — remains available as a transition path
  for anyone not yet on the server model.

The invariant across all of these: **creating the device (one-time power-on init/reset) is
distinct from a client attaching.** The card is initialized once, by whoever creates it; every
client — including the first — only attaches and never re-initializes state.

### 6.2 How UMD connects to the server

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

### 6.3 Who kills the server?

Because the card's lifetime is decoupled from clients, **a client detaching or exiting must
never tear the server down** (this is the key change from today, where a client going away
kills the simulator). Two separate concerns decide what happens.

**Teardown triggers — when the card decides to shut down:**

- **Explicit stop** — the owner (person/CI/service that created it) shuts it down when done.
  Natural fit for the offline model.
- **Policy-based** — e.g. linger for a grace period after the last client detaches, then exit;
  or stay up indefinitely for a long-lived shared card. A bounded linger keeps CI from leaking
  server processes while still surviving the brief gap during a process-to-process handoff.

**Client accounting — keeping an honest count of who is attached:**

The card tracks how many clients are attached, because the policies above depend on knowing
when the last one leaves. A clean exit detaches and releases its reference; a **crashed**
client never sends that detach, so the server detects the dropped connection and releases the
reference on its behalf. Crash detection is *only* bookkeeping — a crash is treated exactly
like a clean detach (release one reference, nothing more), so it neither leaks a reference
(keeping the card alive forever) nor wrongly brings the card down.

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
