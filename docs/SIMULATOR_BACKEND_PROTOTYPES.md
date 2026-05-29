<!--
SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
SPDX-License-Identifier: Apache-2.0
-->

# Simulator Backend Separation — Prototypes

Two throwaway spikes that make the [design](SIMULATOR_BACKEND_SEPARATION_DESIGN.md)
concrete. Both compile. Neither is for merge — they exist to compare the **Part A**
(no simulator-side changes) and **Part B** (simulator side already cooperative)
approaches against real, diffable code.

| | Prototype A — Part A | Prototype B — Part B |
|---|---|---|
| Branch | `pjanevski/sim-backend-prototype-a` | `pjanevski/sim-backend-prototype-b` |
| Premise | Works against **today's** `libttsim.so` (11 symbols); zero sim cooperation | Assumes the sim **already exports** a cooperative ABI |
| Build | Clean (`libtt-umd.so`) | Clean — builds, links, loads (incl. a mock `.so`) |

## Prototype A — UMD-only seam

Separation achievable **unilaterally**, today.

- `ISimBackend` logical interface + `LibTTSimBackend` wrapping the existing 11 symbols.
- `TTSimCommunicator` reduced to a thin facade owning a `unique_ptr<ISimBackend>`; all
  existing call sites (`TTSimTTDevice`, TLB handle/window) compile **unchanged**.
- Capability detection via per-backend `capability → required-function-set` tables with
  **all-or-nothing** `dlsym` resolution (`CapabilityReq`).
- Backend selection by **full-set probing** (registry of candidates; the backend whose
  minimum required set fully resolves wins, tie-broken by most symbols matched). Second
  backend left as a visible stub.
- Multichip is modeled as a capability that is simply **absent** on today's binary
  (`libttsim_create_device_by_id` etc. don't resolve), with a clean call-site fallback.

**Files:** `device/api/umd/device/simulation/{sim_backend.hpp, libttsim_backend.hpp,
sim_backend_registry.hpp}`, `device/simulation/{libttsim_backend.cpp,
sim_backend_registry.cpp}`, plus facade edits to `tt_sim_communicator.*` and a fallback
call site in `tt_sim_tt_device.cpp`.

**Limitations it surfaced:**
- `dlsym` presence is the *only* signal — it **cannot detect semantic divergence**
  (same symbol, changed meaning/signature → a wrong-ABI call, silently).
- Function signatures are hard-coded casts in the backend; a renamed-but-equivalent
  symbol is a one-line table edit, but a changed signature is silently wrong.
- Selection is exercised but not truly contended with only one real backend today.

## Prototype B — sim-cooperative seam

The **target** shape once the simulator cooperates. Assumed ABI lives in
`device/api/umd/device/simulation/backend/libttsim_abi.h` (17 `extern "C"` symbols),
with an opaque `libttsim_device_t*` handle on every per-device call:

- Identity/versioning: `libttsim_variant()`, `libttsim_abi_version()`.
- Self-declared features: `libttsim_query_capabilities()` (bitmask) — no per-symbol
  probing.
- Per-device handle on all I/O + callbacks, with a `void* user` context on DMA
  callbacks.
- Self-description: `libttsim_get_arch()`, `libttsim_get_soc_descriptor()`.
- Graceful failure: `libttsim_init()` status + `libttsim_last_error()`.

Backend is selected by **authoritative variant** + an **ABI-version gate** (refuse on
mismatch); capabilities come from the self-declared mask. A **mock `libttsim.so`**
(`tools/mock_libttsim/mock_libttsim.cpp`) implements the ABI so the prototype compiles
and round-trips (smoke test: two devices kept independent DMA callbacks).

**What disappears vs. today:**

| Pain point (today) | After Part B |
|---|---|
| `TTSimCommunicator::callback_instance_` process-global static | Gone — backend state is instance-owned |
| Select-before-every-I/O dance | Gone — the device handle is an argument |
| Last-chip-wins DMA callback bug | Fixed — callbacks carry a per-device `user` pointer |
| `soc_descriptor.yaml` next to the `.so` + extension-based dispatch | Gone — arch/descriptor come from the `.so`; selection is on `variant` |

**Cost it surfaced:** UMD now trusts the sim for its identity, capabilities, arch, and
SOC descriptor; the `abi_version` gate + `last_error` are the safety net. The ABI
header becomes a shared contract requiring coordinated version bumps between UMD and
both sim repos.

## Takeaway

- **A is the shippable floor** — real separation with no cross-repo coordination, but
  permanently blind to semantic divergence.
- **B is the target** — dramatically cleaner and actually *fixes* known bugs, but every
  benefit is rented from the sim teams adopting the contract.

They are not either/or: A can ship now; B is what A grows into as each Part B affordance
(design §7) lands in *both* simulator repos. The prototypes confirm the design's
with/without-sim-changes split is the right axis.
