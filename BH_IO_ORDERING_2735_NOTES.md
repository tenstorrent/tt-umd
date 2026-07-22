# BH IO ordering / membar (issue #2735) — investigation notes & repro

> Throwaway handoff doc for the repro branch. **Not for merge.** Captures the
> analysis behind the `ClusterBH.WriteWriteOrderingProbe` test so the work can
> continue on a machine with Blackhole silicon.

## The problem

On Blackhole we use **dynamic VC for all host traffic** (`get_static_vc() == false`)
to work around a HW bug: a single static VC carrying **both reads and writes** (which
is what happened when `static_vc=true` with buddy/class defaulting to 0) crashed the
host. Dynamic VC dodges the crash but **loses NoC write→write ordering**, so our
memory barrier may not actually guarantee IO ordering. That is issue #2735.

## Two independent ordering layers

1. **PCIe-tile / AXI** — the `tlb_data.ordering` field (Default / Strict AXI /
   Posted / Counted). Governs how the tile injects AXI→NoC and its write-ack policy.
2. **NoC virtual channel** — `static_vc` + `static_vc_class` / `static_vc_buddy`.
   Governs ordering along the NoC path to the destination tile. **This is the broken
   layer on BH today.**

### Ordering modes (from `WormholeB0/PCIExpressTile/TLBs.md`, BH analogous)

Our enum: `Relaxed=0` (=Default), `Strict=1` (=Strict AXI), `Posted=2`. Counted (3)
is BH/Grendel-only and undocumented in the ISA pages we have; we never set it.

| 1st → 2nd | PCIe requires | Strict AXI | Default (our data path) | Posted Writes |
|---|---|---|---|---|
| Read → Read | reorder allowed | Ordered | Reorder possible | Reorder possible |
| Read → Write | reorder allowed | Reorder possible | Reorder possible | Reorder possible |
| Write → Read | must stay ordered | Ordered | **Ordered** | ⚠️ Reorder possible |
| Write → Write | must stay ordered | Ordered | **Ordered iff `static_vc`** | Ordered iff `static_vc` |

Two ways to get Write→Write ordering:
- **Strict AXI** — serialize: wait for each write's NoC-ack before the next (ordered
  without `static_vc`, but throttles throughput). Used by the low-volume register path.
- **Default + `static_vc`** — pipeline writes onto one static VC; ordered along the
  path, no per-write ack wait. What the data path wants — and what's broken on BH
  (`static_vc=0`).

**Write→Read is already ordered in Default** — the tile withholds the read until the
write's NoC commit-ack returns, so the read can't overtake the write **on the NoC**
either (it isn't injected until the write has landed). This is *not* true in Posted
mode: there the ack returns as soon as data enters the NoC, so the read is released
early and can overtake the write on the NoC (and `static_vc` can't help — reads and
writes ride different static VCs).

Where the Write→Write overtake physically happens (Default): order is preserved up to
and through the PCIe-tile NIU; the reorder happens **on the NoC** (routers + recipient
NIU) purely because dynamic VC can put the two writes on **different VCs**. `static_vc`
pins both to one VC + one route → no reorder. Caveats even then: only same-path
(reconfiguring the window to a different target tile breaks cross-reconfig ordering),
and the recipient NIU only guarantees certain same-VC orderings (per-byte for
overlapping L1 ranges).

## static_vc buddy/class

From `HostToDeviceTLBs.md` (BH bit offsets): 2 MiB TLB `static_vc`=73,
`static_vc_buddy`=75, `static_vc_class`=76–77; 4 GiB TLB 62 / 64 / 65–66. WH's PCIe
tile **hardcodes** buddy=0 for writes, buddy=1 for reads, class=0b10 for multicast,
0b00 otherwise. BH exposes these as config bits. The fix = replicate WH's mapping on
BH so no single static VC ever carries both directions (dodges the crash) while each
direction stays internally ordered.

## The membar and why it's a latent bug

`LocalChip::set_membar_flag()` is **write-then-read-back poll**, not a HW fence: write
a flag to `barrier_addr` on each core, then spin-read until every core reads it back.

- Correctness rests on **Write→Write ordering** (data writes land before the flag
  write, per destination core) — so observing the flag ⟹ data committed.
- The read-back is just a completion probe (write→read, already ordered) — it does
  **not** need write→read ordering across the R/W VC split.

**Today (dynamic VC):** the flag write can overtake the data writes on the NoC, so the
read-back confirms only that the *flag* landed, not the data → the barrier property is
not provided. **After the fix (static_vc + buddy split):** data and flag both ride
buddy=0 to the same core → ordered → the membar becomes a true barrier. Multicast is a
residual gap (mcast data on class 0b10 vs unicast flag on 0b00 = different VC) — same
as WH, so parity not regression.

### Why nothing is red today

`ClusterBH.MultiThreadedMemBar` only tests (a) thread-safety and (b) write →
read-back of the **same address** (Write→Read, already ordered in Default regardless
of VC). It never exercises the Write→Write property the membar depends on. So a green
suite is fully consistent with the barrier being broken. The gap is **device-
observable only** — a pure host read-back is serialized by PCIe and masks it.

## Design direction (agreed)

- **Approach A**: turn `static_vc` on for the **cached** windows and pin buddy/class by
  direction, behind a single helper (e.g. `apply_static_vc(config, Direction)`), so no
  call site hand-codes buddy/class and none can put a read on the write VC.
- Scope now: **cached windows only** — data path (`write/read_block_reconfigure`,
  `noc_multicast_write_reconfigure`), register path (`write/read_to_device_reg`), and
  DMA/sysmem cached windows. **Leave static/persistent TLBs (`configure_tlb`) alone.**
- **Gotcha:** do **not** flip `get_static_vc()` globally — `configure_tlb` reads it, so
  flipping re-arms the shared-VC crash on the persistent path. Enable static VC only via
  the new helper at the cached sites; leave `get_static_vc()` returning false for now.
- Other options considered: **B** dedicate windows per direction (the issue's own
  suggestion; hedges the "crash is TLB-scoped, not VC-scoped" hypothesis + kills
  reconfig thrash); **C** = A+B; **D** static-on-writes-only. Root cause is unconfirmed
  ("crash was visible when `static_vc=true`", when buddy defaulted to 0 for both) so A
  is the primary, B is the fallback if A still crashes on silicon.
- Existing prototype (full, all paths incl. the buddy/class fields + flips):
  branch **`pjanevski/tlb-static-vc-buddy-class`**, commit `2c512447`. Note it flips
  `get_static_vc()` globally, so it re-arms the crash on the persistent path — the
  scoped design above avoids that.

## The repro test — `ClusterBH.WriteWriteOrderingProbe`

Added in `tests/blackhole/test_cluster_bh.cpp`. **This branch has NO static_vc fix** —
it's the dynamic-VC baseline to see whether the reorder is observable on silicon.

Pattern (single Tensix core, two **separate** raw TLB windows = separate AXI ids):
- Writer: `fill DATA[8K words]=k ; sfence ; FLAG=k` (k monotonic), dynamic VC + Relaxed.
- Reader: `f=read(FLAG); tail=read(DATA_last_word)`. Invariant if ordered: `tail >= f`.
  A `tail < f` sample = FLAG overtook DATA = **Write→Write reorder observed**.

Separate windows are essential: a shared window is one AXI id, and the tile then
enforces Default Write→Read ordering, masking the reorder. Blocking MMIO reads order
the reader's own two reads, so there are no false positives.

**One-sided:** an inversion proves breakage; zero inversions does NOT prove safety (the
HW may keep same-src/same-dst writes ordered in practice — e.g. it may not split
consecutive same-dest writes across VCs). `EXPECT_EQ(inversions, 0)` so it goes red
today if the HW reorders, and must be green after the fix.

### Build & run

```
cmake --build build --target unit_tests_blackhole
./build/test/umd/blackhole/unit_tests --gtest_filter='ClusterBH.WriteWriteOrderingProbe'
```

Requires KMD ≥ 1.34 (raw TLB-alloc ioctl); the test skips otherwise. Tunables in the
test: `kDataWords` (payload size → reorder window), `kIterations`.

### Interpreting results

- **Fires (inversions > 0):** reproduces #2735. Next: apply the scoped Approach-A fix
  (needs the `static_vc_buddy`/`static_vc_class` fields) and re-run — should go to zero,
  and confirm no host crash. That single test then becomes the red→green regression.
- **Never fires:** reorder isn't observable this way on this silicon; the fix is still
  justified on spec-correctness + WH parity, but we'd need a device-side consumer
  (RISC kernel spinning on the flag then checking data) to demonstrate it directly.

## Next steps

1. Run the probe on BH silicon (this branch) — does it fire?
2. If yes: implement scoped Approach A (helper + `static_vc_buddy`/`class` fields on the
   cached paths only), re-run to confirm red→green + no crash.
3. Extend the probe with a static-VC variant (writer buddy=0 / reader buddy=1) once the
   tlb_data fields exist, to assert no-fire + no-crash directly.
