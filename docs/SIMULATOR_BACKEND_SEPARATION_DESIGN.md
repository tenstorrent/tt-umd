<!--
SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
SPDX-License-Identifier: Apache-2.0
-->

# Simulator Backend Separation — Design

**Status:** Draft for review
**Date:** 2026-05-29
**Scope:** UMD `device/` simulation layer (the `tt_sim_*` / `TTSim*` path)

## 1. Problem

Two simulators implement substantially the same device but diverge in features and
APIs over time:

- **`tenstorrent/ttsim-private`** — the full-system reference simulator.
- **`tenstorrent/craq-sim`** — described upstream as a *"fork mirror of
  ttsim-private for ISA feature expansion work"*; the scratchpad where parity/feature
  work lands first before (some of it) hardens back into `ttsim`.

Both build an artifact literally named `libttsim.so`, and **today their exported C ABI
is byte-for-byte identical** (`src/libttsim.map` lists the same 11 `libttsim_*`
symbols in both repos). UMD's existing `TTSimCommunicator` therefore already loads
either one without modification.

The risk is forward-looking: the two repos will add features **independently and
differently**. craq-sim is already ahead — see PR
[tt-umd#2729](https://github.com/tenstorrent/tt-umd/pull/2729), which adds multichip,
eth-MAC/virtual-switch wiring, fabric registration, and DRAM teleport, introducing
~15 new `libttsim_*` symbols. We do **not** know how (or whether) ttsim-private will
add the *same logical features*; it may use different symbol names, a different ABI
shape, or a fundamentally different model (e.g. separate `dlopen`s per chip instead
of a shared process-global handle).

We want UMD to support both simulators while **avoiding conflict between features/APIs
coming from the two**, and without letting each simulator's idiosyncrasies sprawl
across core UMD code.

### 1.1 The three flavors of divergence

| Flavor | Example | Detectable by `dlsym` probing alone? |
|---|---|---|
| 1. Different symbol names, same logical op | craq `libttsim_trace_start` vs ttsim `libttsim_begin_trace` | Yes — probe for both, use whichever resolves |
| 2. Same name, different signature | both export `libttsim_foo`, `(u32)` vs `(u64)` | **No** — `dlsym` yields a `void*`; wrong cast is UB |
| 3. Same name + signature, different behavior | both export `libttsim_clock`, semantics differ | **No** — invisible to probing |

Capability probing alone only safely covers flavor 1. Flavors 2 and 3 require an
explicit, machine-checkable identity/version — which is one of the sim-side
affordances in Part B.

## 2. Goals / Non-goals

**Goals**
- A single, sim-agnostic seam in UMD that both simulators plug into.
- New features from one simulator land in *its* backend without touching the other or
  the shared layers.
- Symbol-level divergence (new/renamed/optional `libttsim_*` symbols) is contained in
  one place with an explicit required-vs-optional contract.
- A reliable way to identify which simulator/ABI is loaded, with a graceful fallback
  when the simulators cannot (yet) cooperate.
- Non-breaking: the existing single-chip path and its tests keep working unchanged.

**Non-goals (this iteration)**
- Refactoring `Cluster` — PR #2729's eth-MAC wiring pre-pass and the
  `register_sim_fabric_*` methods stay where they are *for now* (see §6, deferred).
- Build-time exclusion of a backend — runtime selection only; design leaves room to
  add build-time selection later (§6).
- Touching the RTL-simulation path (`rtl_sim_*`); it is a separate transport and out
  of scope here.
- Implementing ttsim-private's version of any craq-only feature — that binding is
  written when/if ttsim-private ships it.

## 3. Governing principle

> **UMD defines logical capabilities in its own vocabulary. Each backend owns the
> entire implementation of a capability. UMD never bakes in one simulator's symbol
> names or mechanism.**

This is an anti-corruption layer. The interface UMD depends on speaks in features
("advance the clock", "set up multichip for this topology", "fast DRAM access"),
never in `libttsim_*` symbols. Two simulators solving the same problem differently are
two different implementations behind the same logical method — they cannot collide
because they are different objects.

The principle's end-state implication is that even cross-chip *orchestration* (e.g. PR
#2729's eth-MAC wiring, currently in `Cluster::Cluster`) belongs inside the owning
backend, because it is craq's bring-up mechanism, not a universal one. This iteration
**defers** that relocation to keep the blast radius small (see §6); the principle still
sets the direction.

## 4. Organizing axis: with vs without sim changes

The design splits cleanly along one axis — **does it require the simulator repos to
add or change something?**

- **Part A (§5)** ships **without any sim-side change.** It uses only what
  `libttsim.so` already exports and needs no cross-repo coordination. This is the bulk
  of the separation work and can land immediately.
- **Part B (§7)** is a **menu of sim-side affordances.** Each is independently
  adoptable, removes a concrete UMD pain point, and is ranked by leverage. None blocks
  Part A; each one *upgrades* it (e.g. authoritative identification instead of
  inference, or deleting process-global state instead of guarding it).

---

# Part A — UMD changes WITHOUT sim changes

Everything here is unilateral: UMD-only, works against today's `libttsim.so`.

## 5.1 The `ISimBackend` seam (architecture)

The seam sits **below `Cluster`**, at the communicator/backend layer.
`TTSimCommunicator` remains the stable facade that `Cluster` and `TTSimTTDevice`
already call; internally it delegates feature implementation to an `ISimBackend` it
owns. This contains the change: no call-site churn in `Cluster` or the `TTSimTTDevice`
hot path.

```
        Cluster / TTSimChip / TTSimTTDevice
                    │  call (unchanged surface)
                    ▼
            TTSimCommunicator              ← stable facade; picks + delegates
                    │  owns
                    ▼
              ISimBackend                  ← logical, sim-agnostic ops
              ├─ read/write tile, pci mem, advance clock   (common)
              ├─ supports(Capability)                       (introspection)
              ├─ setup_multichip(topology)                  (orchestration owned here)
              ├─ dram_fast_access(...) -> optional          (optional capability)
              └─ wire_fabric_*(...)                         (no-op if unsupported)
                  ▲                         ▲
        ┌─────────┘                         └──────────┐
   TtsimBackend                                   CraqSimBackend
   - complete, INDEPENDENT binding of               - complete, INDEPENDENT binding of
     ttsim's own symbol names + signatures            craq's own symbol names + signatures
   - shares NO symbol set with the other            - implements multichip shared-dlopen,
     backend; the only shared thing is the            eth-MAC/switch wiring, fabric reg,
     logical ISimBackend interface                    DRAM teleport (ported from #2729)
```

There is **no shared symbol base.** The two simulators may be totally out of sync —
even the symbols UMD considers "stable" today could be renamed independently. Each
backend therefore declares its *entire* symbol table on its own and maps it onto the
shared logical interface. The interface is the only contract; symbols never are.

## 5.2 Backend split (independent bindings)

- `ISimBackend` is the interface UMD depends on; its methods are **logical features**,
  never symbol names.
- `TtsimBackend` and `CraqSimBackend` are each a *self-contained* binding: each owns its
  full set of function-pointer typedefs (with its own signatures) and its own
  `dlsym` resolution. Neither derives a "common" symbol set from the other.
- A backend implements only the logical methods its sim supports; the rest report
  "unsupported" (so e.g. multichip is a no-op on a backend whose sim lacks it).

## 5.3 Per-backend symbol table with tiers

Each backend declares its *complete* symbol table rather than hand-rolling `dlsym`:

- **Required** — resolved via the throwing `DLSYM_FUNCTION` macro; a missing required
  symbol means this backend does **not** match this `.so` (used for selection, §5.5).
  Defines the backend's minimum contract.
- **Optional** — resolved via raw `dlsym`; `nullptr` means that capability is absent
  *within* this backend. Each optional symbol (or group) maps to a `SimCapability`.

This formalizes the required/optional split that PR #2729 expresses only in comments,
and — because the table is per-backend — accommodates two sims that name the same
operation differently.

## 5.4 Capability model: per-backend required-function sets

A capability is **logical**; its **binding** (which symbols implement it, and how they
are called) is per-sim. Each backend declares, for every capability, the **set of
functions that must *all* resolve** for that capability to be present — declared on the
backend itself, so two sims can require different names for the same logical feature:

```cpp
enum class SimCapability { Multichip, DramTeleport, FabricRegistration, EthSwitchWiring /* ... */ };

// Per-backend: a capability -> the functions that must ALL dlsym non-null.
struct CapabilityReq { SimCapability cap; std::initializer_list<const char*> required_fns; };

// CraqSimBackend's OWN map (TtsimBackend declares its own, possibly different names):
static constexpr CapabilityReq kCraqCaps[] = {
    {SimCapability::Multichip,         {"libttsim_create_device_by_id",
                                        "libttsim_select_device_by_id",
                                        "libttsim_clock_all_devices"}},
    {SimCapability::DramTeleport,      {"libttsim_dram_core_rd_bytes_by_id",
                                        "libttsim_dram_core_wr_bytes_by_id"}},
    {SimCapability::EthSwitchWiring,   {"libttsim_switch_reset", "libttsim_switch_register",
                                        "libttsim_configure_eth_link_virtual",
                                        "libttsim_switch_register_peer"}},
    {SimCapability::FabricRegistration,{"libttsim_switch_register_fabric_node_id",
                                        "libttsim_switch_register_fabric_endpoint_direction"}},
};

// at load — present iff EVERY function in the set resolves (all-or-nothing):
for (auto& req : kCraqCaps)
    capabilities_.set(req.cap, std::all_of(req.required_fns.begin(), req.required_fns.end(),
                                           [&](auto s){ return dlsym(handle, s) != nullptr; }));
```

**All-or-nothing** is deliberate: a partially-resolved set (e.g. read present, write
missing) reports the capability *unsupported* rather than half-wiring it and crashing
at the first missing call. Call sites ask `backend->supports(cap)` (or rely on an
`optional`/bool return) and apply **one** centralized fallback policy — replacing the
scattered `if (!multichip_mode_ || pfn_... == nullptr) return false;` checks in PR
#2729. The per-capability set is exactly the `probe()` of a candidate binding when the
same capability has divergent implementations across sims.

This gives **two levels** of "functions that must all resolve":

| Level | Set | Purpose |
|---|---|---|
| Backend selection (§5.5) | the backend's *minimum* required symbols | qualifies "is this `.so` this backend at all" |
| Capability (above) | per-capability required-function set | gates each optional feature *within* the chosen backend |

### When two sims implement the same capability differently

| The two APIs differ by… | Resolvable in Part A (probing)? | How |
|---|---|---|
| **Symbol names** (sims out of sync — the expected case) | ✅ Yes | Each binding probes its own names; only its sim matches. *More* divergence → cleaner separation. |
| **Signature/behavior, same names** (flavor 2/3) | ❌ No | Both bindings probe true on identical names → ambiguous; wrong cast is UB. Needs §7.1. |
| **Both name sets present** (a sim ships old + new) | ⚠️ Priority rule | Prefer most-specific binding; clean only with §7.1. |

## 5.5 Selection by full-set probing

Selection is entirely derived from the loaded `.so` — no env var, no build flag; the
binary that is loaded *is* the selector. Each registered backend probes the handle:

```
for each backend:  score = backend.probe(handle)   // do MY required symbols resolve? how many?
pick the fully-satisfied backend with the highest specificity (most symbols matched)
```

- **Sims fully out of sync (different names):** exactly one backend's required set
  resolves → clean, unambiguous selection, **no sim cooperation needed.**
- **Today (byte-identical ABIs):** both backends match; they are behaviorally
  identical, so either is correct. The specificity tie-break (and later §7.1's variant)
  disambiguates as they diverge.
- **Same names, different meaning (flavor 2/3):** probing cannot tell the bindings
  apart. UMD logs `variant = Unknown` and accepts the residual risk; Part B §7.1
  closes it.

## 5.6 Centralized gates and naming

- The DRAM-teleport env gate is read in **one** place with **one** canonical name.
  (PR #2729 has a mismatch: the body says `TT_UMD_SIM_DRAM_TELEPORT`, the code reads
  `TT_SIMULATOR_DRAM_TELEPORT`. The spec picks one canonical name and documents it.)
- All capability fallbacks (DRAM teleport → tile path, unsupported fabric reg → no-op)
  live behind the backend interface, not in `TTSimTTDevice`/`Cluster`.

## 5.7 Re-home PR #2729

PR #2729's symbol-level features move into `CraqSimBackend` with no new behavior in the
shared layers (full mapping in §8). `Cluster` call sites are unchanged.

**Outcome of Part A:** the full separation seam, both backends, and the contained PR
#2729 features — shippable with zero cross-repo coordination.

---

# Part B — UMD changes ENABLED by sim changes

A menu of affordances the simulator repos could export to make UMD simpler and safer.
Each is **independent and optional**; each removes a specific pain that Part A can only
*work around*. Ranked by leverage. The ask, for any UMD-facing addition, is that
**both** sim repos adopt it (or neither), so UMD does not fork on it.

## 7.1 ABI version + variant symbol — *highest leverage*

`libttsim_variant()` (a stable identity string/enum: which simulator this is) plus
`libttsim_abi_version()`, with the contract that `abi_version` increments whenever any
exported symbol's signature or semantics change. `variant` is what lets UMD pick the
right backend even when two sims collide on symbol *names* but differ in meaning — the
one case full-set probing (§5.5) cannot resolve on its own.

- **UMD pain removed:** the only defense against **flavor-2/3** divergence (§1.1).
  Turns backend selection from inference into an authoritative check, and turns PR
  #2729's *"v3.5 libttsim ABI"* folklore (there is no version symbol today; "v3.5" is
  inferred from which symbols happen to be present) into a checked value UMD can
  assert and refuse/warn on mismatch.

## 7.2 Single capability-query entry point

`libttsim_query_capabilities()` returning a bitmask/struct, or
`libttsim_has_feature("multichip")`.

- **UMD pain removed:** decouples UMD from individual optional-symbol *names*, so a
  rename (**flavor 1**) doesn't require a UMD change. One contract symbol replaces N
  per-feature `dlsym` probes; the sim declares its own capabilities.

## 7.3 Self-description: arch + SOC descriptor from the `.so`

`libttsim_get_arch()` and/or `libttsim_get_soc_descriptor()` (returns the YAML/struct).

- **UMD pain removed:** today the `.so` **must** live in a directory containing a
  matching `soc_descriptor.yaml`, and dispatch keys off the file *extension*
  (`SimulationChip::get_soc_descriptor_path_from_simulator_path`,
  `SimulationChip::create`). A self-describing `.so` removes the path convention and
  the extension-based dispatch, and guarantees the descriptor matches the binary.

## 7.4 Per-device context handle + per-device callbacks — *big simplification*

Take a `Device*`/context handle (and a user-data `void*`) on every I/O and callback
registration: `libttsim_pci_mem_rd_bytes(dev, ...)`,
`libttsim_set_pci_dma_mem_callbacks(dev, rd, wr, user)`.

- **UMD pain removed:** eliminates PR #2729's **process-global statics**
  (`s_shared_handle_`, `s_shared_refcount_`, static `device_lock_`), the
  **select-before-every-I/O** dance (`select_chip_if_needed`), the global lock that
  serializes all chips, and the documented **last-chip-wins DMA callback bug**
  (`callback_instance_` is process-global, so only the last chip to register receives
  correct callbacks). Per-device context makes multichip natural instead of a
  shared-state workaround.

## 7.5 Init/shutdown status + error reporting

`int libttsim_init(...)` returning a status (today it returns `void`), plus
`const char* libttsim_last_error()`.

- **UMD pain removed:** UMD can convert simulator faults into UMD exceptions and fail
  gracefully, instead of relying on the sim to abort the process. Enables a real
  version-negotiation handshake (`libttsim_init(umd_requested_abi)` → accept/reject).

## 7.6 Native multicast

`libttsim_tile_multicast_wr_bytes(...)`.

- **UMD pain removed:** `SimulationChip::noc_multicast_write` currently emulates
  multicast with a **loop of unicasts** and a hardcoded **Blackhole `x==8/9` skip**
  workaround. A native multicast removes both the loop and the arch-specific hack.

## 7.7 Clock / progress query

`uint64_t libttsim_get_clock()` and a first-class hang-watchdog control (craq exposes
this only via the `TTSIM_HANG_WATCHDOG_CLOCKS` env var today).

- **UMD pain removed:** `SimulationChip::get_clock()` is stubbed to `return 0`; a query
  un-stubs it. A watchdog API lets UMD surface no-progress hangs as errors rather than
  depending on an env var read inside the sim.

> Lower-priority candidates (noted, not pursued now): exposing reset/power/ARC
> semantics so UMD need not stub `send_tensix_risc_reset` / `deassert_risc_resets` /
> `set_power_state` / `arc_msg` as no-ops; native memfd-based secure load to simplify
> the `copy_sim_binary` path.

---

## 8. Mapping PR #2729 onto this design

PR #2729, re-homed, becomes **a `CraqSimBackend` implementation** with no new behavior
in the shared layers. The "Part B upgrade" column shows what a sim-side affordance
would further simplify:

| #2729 feature | Today (PR) | Part A (no sim change) | Part B upgrade |
|---|---|---|---|
| Multichip shared dlopen | statics + `multichip_mode_` in `TTSimCommunicator` | `CraqSimBackend` owns it; `supports(Multichip)` | §7.4 per-device handle deletes the statics |
| Eth-MAC / switch wiring | comm methods driven by `Cluster` pre-pass | backend methods; orchestration relocation **deferred** (§6) | §7.4 simplifies bring-up |
| Fabric registration | optional `dlsym` + `Cluster` passthroughs | optional capability; passthroughs unchanged for now | §7.2 decouples from symbol names |
| DRAM teleport | hot-path branch in `TTSimTTDevice`, env-gated | `backend->dram_fast_access()` → `optional`; gate centralized | §7.3 arch self-description |
| Required/optional tiers | hand-rolled in comments | declared symbol table (§5.3) | §7.1 ABI version |
| "v3.5 ABI" | folklore from symbol presence | inferred via probing; `Unknown` logged | §7.1 makes it authoritative |

## 9. Scope boundaries & deferred work

**In scope (this iteration)** — all of Part A (§5).

**Deferred (explicitly out of scope, per review)**
- Moving PR #2729's eth-MAC wiring out of `Cluster::Cluster` into the backend. Left
  as-is for now to keep blast radius small; tracked as follow-up. The
  `register_sim_fabric_*` `Cluster` methods and the `TT_UMD_BUILD_SIMULATION`
  `PRIVATE`→`PUBLIC` change from #2729 also stay as-is, with the `PUBLIC` flip noted
  as a follow-up risk (consumer API should not shift on a sim build flag).
- Build-time backend exclusion. Runtime selection ships now; the seam is designed so a
  build flag that compiles only one backend is a later, localized addition.
- All of Part B — pursued opportunistically as the sim repos adopt each affordance.
- ttsim-private's binding of any craq-only feature — a new `TtsimBackend` method body
  when ttsim-private ships it; nothing else changes.

## 10. Phasing

1. **Part A, step 1 — seam, no behavior change.** Add `ISimBackend` + capability model
   + selection; wrap the current path as `TtsimBackend`. All existing tests green.
2. **Part A, step 2 — `CraqSimBackend`.** Port PR #2729's features; route
   `TTSimCommunicator`'s extended methods to the backend. `Cluster` call sites
   unchanged.
3. **Part B — opportunistic.** As each sim affordance (§7) lands in *both* repos, UMD
   adopts it and deletes the corresponding Part-A workaround. §7.1 first (it gates the
   authoritative-identification upgrade).
4. **Future UMD-side.** ttsim multichip binding; relocate `Cluster` orchestration;
   optional build-time selection.

## 11. Testing

- **Backend selection** (unit, fake `.so`): capability set correctly derived from
  resolved symbols; (with §7.1) identity present/absent and mismatch assertion fires.
- **Capability gating:** an unsupported capability yields the documented fallback /
  no-op (e.g. DRAM teleport falls back to the tile path).
- **Regression:** existing `TTSimDeviceIOFixture` and PR #2729's
  `test_sim_multichip.cpp` retargeted at `CraqSimBackend`; single-chip path unchanged.
- **CI:** the existing workflow downloads a `libttsim.so` and runs the logical tests
  against whichever backend it resolves to; later a matrix can run both.

## 12. Risks & open questions

- **Will both sim repos adopt the Part B affordances (esp. §7.1)?** If not, UMD stays
  on the Part A fallback path and accepts residual flavor-2/3 risk. Needs sim-team
  agreement; the with/without split (§4) exists precisely so Part A does not block on
  this.
- **Same logical feature, incompatible models.** If ttsim-private implements multichip
  with a model that does not fit `setup_multichip(topology)` (e.g. no global clock),
  the logical interface may need coarsening or the orchestration pushed entirely into
  the backend (reinforces the deferred `Cluster` relocation).
- **`TTSimCommunicator` as facade vs. exposing `ISimBackend` directly.** Facade keeps
  `Cluster`/`TTSimTTDevice` untouched now; revisit if the facade becomes a pass-through
  with no value.
