# Quasar NOC Address Translation Table (ATT)

Handoff and reference for the Quasar/Grendel ATT support in UMD. Written for roadmap planning:
what the hardware is, what UMD implements, what it deliberately does not, and where the risk sits.

Scope: the **NoC** ATT on the qsr.s1 model. The SMN ATT is a separate mechanism (see §3.5) and UMD
does not model it. Statements about register contents below were decoded from the generated SMC
boot firmware, not from a datasheet — see §7 for which sources are authoritative and which are
stale.

**Status: complete and merged.** tt-umd-internal issue #23 ("Enable ATT access on Grendel") landed
as a six-PR stack, #3440 → #3441 → #3442 → #3443 → #3444 → #3409, all merged to `main`.

---

## 1. Why ATT exists

On Wormhole and Blackhole the destination core travels **out of band**: software programs a TLB
register with the target `(x, y)` and then writes a core-local address. Address and coordinate are
two separate things.

Quasar inverts this. **The address *is* the coordinate.** Software emits one flat 64-bit address
onto the NoC and hardware — the ATT — decomposes it into a destination tile plus a tile-local
offset. Nothing else on the wire says where the access is going.

So UMD's job is the inverse of the hardware's: given a core and an offset, fold them into the one
flat address the ATT will decompose back into that same core and offset.

---

## 2. Concepts

| term | meaning |
|---|---|
| **window** / mask-table entry | one claimed address range plus the rules for reading the bits inside it |
| **selector** (`ep_id`) | a field cut out of the address that names which endpoint the access is for |
| **endpoint table** | a flat array of destination coordinates, indexed by `table_offset + selector` |
| **`compare` / `mask`** | the window's base, and how many **low** address bits the comparison ignores |
| **`translate_addr` / `BAR`** | whether and how the address is rewritten after a window matches |
| **package frame** | the coordinate frame the endpoint tables are written in (Quasar mesh origin `(3,3)`) |
| **descriptor frame** | the frame UMD's soc descriptor uses (Quasar mesh origin `(1,1)`) |

The two frames are bridged by `package_offset = (2, 2)`, a field of the map.

---

## 3. Hardware representation

### 3.1 Where it lives

The ATT is a register file **inside each NIU** — not a CAM, not a structure in memory. Two blocks
per die:

| block | base | serves |
|---|---|---|
| tile NIU | `0x0201_0000` | Tensix / Dispatch |
| SMU NOC2AXI | `0x1223_0000` | the SMU |

The two programs are **byte-identical** — same offsets, no offset where the data differs. The only
difference is the base, plus one extra `ENABLE_TABLES` write on the SMU for DV parity (188 vs 187
register writes). That program is replicated into every NIU that originates global addresses.

### 3.2 Register map within a block

```
+0x0000  ENABLE_TABLES      bit0 = enable address translation, bit1 = enable dynamic routing
+0x0004  CLK_GATING

+0x0030  ┌─ mask entry 0 ─────────────────────┐
+0x0034  │  (unused)                          │
+0x0038  │  MASK_EP_LO    compare[31:0]       │  stride 0x18 (24 bytes)
+0x003C  │  MASK_EP_HI    compare[63:32]      │
+0x0040  │  MASK_BAR_LO   BAR[31:0]           │
+0x0044  └─ MASK_BAR_HI   BAR[63:32] ─────────┘
   ...      16 slots, ending at +0x01B0

+0x2000  ENDPOINT_TABLE[i]  at +0x2000 + 4*i
```

Mask entry *i* is at `base + 0x30 + 0x18*i`; endpoint row *i* at `base + 0x2000 + 4*i`.

> **Naming trap.** `MASK_EP_LO` / `MASK_EP_HI` hold **`compare`**, not an endpoint. The endpoint
> lives in the separate array at `+0x2000`. The names read as if they point at each other.

### 3.3 Packed words

Mask-entry control word — 29 of 32 bits used:

```
 bit  31..29 │  28   │   27..18     │   17..12    │  11..6  │  5..0
      unused │ trans │ table_offset │ ep_id_size  │ ep_idx  │  mask
             │  1b   │    10b       │     6b      │   6b    │   6b
```

`compare` and `BAR` are 64-bit, hence two registers each — that is why one window costs 5 registers.
`table_offset` being 10 bits caps the endpoint array at **1024 rows** architecturally.

Endpoint row — 12 of 32 bits used:

```
 bit  31..12 │  11..6  │  5..0
      unused │    y    │    x
```

`NOC_XY(x,y) = (y << 6) | x`. **Six bits per axis** — the origin of UMD's 64×64 coordinate limit.

### 3.4 What qsr.s1 actually programs

Decoded from the generated boot firmware. **14 of 16 slots programmed; indices 9 and 10 unused.**

```
idx      entry  mask ep_idx  sz  tbl_off  trans            compare             BAR   label
  0 0x10401821    33     32   1       16      1       0x1000000000               0   generic
  1 0x0080469e    30     26   4       32      0       0x1200000000               0   generic-alt
  2 0x0100559b    27     22   5       64      0       0x1280000000               0   Mimir CCE CSR/SRAM
  3 0x018046df    31     27   4       96      0       0x1300000000               0   Mimir DDR small
  4 0x1200661e    30     24   6      128      1      0x10000000000               0   TensixNEO L1      ← UMD WORKER
  5 0x01805866    38     33   5       96      0    0x1000000000000     0x800000000   Mimir DDR large   ← UMD DRAM
  6 0x00401c31    49     48   1       16      0    0x2000000000000               0   Athena LPDDR[0]
  7 0x00401c31    49     48   1       16      0    0x4000000000000               0   Athena LPDDR[1]
  8 0x109c1c31    49     48   1       39      1    0x6000000000000 0x2000000000000   Athena LPDDR[2]
 11 0x160066e1    33     27   6      384      1       0x1c00000000               0   Quasar secondary Config
 12 0x170066e1    33     27   6      448      1       0x1e00000000               0   slot-12
 13 0x04000014    20      0   0      256      0           0x100000               0   slot-13
 14 0x140066e1    33     27   6      256      1       0x1800000000               0   per-tile Config   ← UMD FULL_TILE
 15 0x150066e1    33     27   6      320      1       0x1a00000000               0   slot-15
```

Endpoint array — **116 rows programmed**, in six disjoint blocks:

```
rows  32-39     entry 1
rows  64-71     entry 2
rows  96-99   ┐ entry 5, D2D lane 0   ┐ entry 3 also starts here
rows 112-115  ┘ entry 5, D2D lane 1   ┘
rows 128-159    entry 4   — the 32 NEO tiles
rows 256-315    entry 14  — all 60 mesh tiles
```

Observations that matter downstream:

- **There is one endpoint array, shared by all windows.** Entries 0/6/7 all point at row 16;
  entries 3 and 5 both start at row 96 and overlap on rows 96–111. A window owns a *starting
  index*, not private rows.
- **The D2D lane split is visible in the layout.** A Mimir fronts its GDDR over two D2D links, and
  selector bit 4 picks between them — rows 96–99 and 112–115, sixteen apart.
- **Entry 13 has `ep_id_size = 0`** — a window with no selector field, resolving to exactly one
  endpoint. This is a real programmed configuration, not a defensive hypothetical.
- **Several windows are live with unpopulated tables.** Entries 0/6/7 (row 16) and 11/12/15 (rows
  320–511) have no endpoint rows written on this config.
- **Five of fourteen slots have no meaningful label** upstream (`generic`, `generic-alt`,
  `slot-12/13/15`). See §6.2.

### 3.5 The SMN ATT is a different mechanism

Easy to conflate; UMD models only the NoC ATT.

| | NoC ATT | SMN ATT |
|---|---|---|
| base | `0x0201_0000` / `0x1223_0000` | `0x0301_2000`, EP at `0x0301_2100` |
| reached via | normal NoC addressing | bit 52 set (`0x10_0000_0000_0000`) |
| window cost | 5 registers, 24 B | **1 register**, 8 B stride |
| `compare` | two registers, 64-bit | **inline, 37 bits** |
| valid bit | none | yes, bit 0 |
| `table_offset` | 10 bits | 7 bits |
| endpoint row | `(y<<6) \| x` | `x \| (y<<6) \| (is_self<<12)` |

That `is_self` bit is the only genuinely per-tile field in either table, and it is the mechanical
reason the NoC ATT's contents are identical in every tile. Per-tile identity otherwise lives in
node-coordinate programming, not in the ATT.

### 3.6 Replication

```
1 endpoint array  per ATT instance
× 2 ATT instances (NoC + SMN)  per NIU
× every NIU that originates global addresses  (+ the SMU)
× 3 chiplets (Quasar, Mimir, Keraunos)
```

All carrying the same 116 shared POR endpoint rows. Quasar additionally carries cross-die delivery
rows, replayed per-initiator by SMN unicast. All of it is written by SMC firmware at boot; nothing
derives it at runtime.

---

## 4. Resolution

### 4.1 Hardware: flat address → (x, y)

Given incoming address `A` at a block based at `B`:

```
0. [B + 0x0000] bit0 must be 1, else the 40-bit address gasket path handles it instead

1. for i = 0..15, IN ORDER, FIRST MATCH WINS:
       E       = [B + 0x30 + 0x18*i]
       mask    = E & 0x3F
       compare = ([B + 0x3C + 0x18*i] << 32) | [B + 0x38 + 0x18*i]
       if (A & ~((1 << mask) - 1)) == compare:  winner, stop
   no match, or more than one: a status bit latches. The access is NOT faulted.

2. ep_idx = (E >> 6) & 0x3F;  ep_id_size = (E >> 12) & 0x3F
   table_offset = (E >> 18) & 0x3FF;  translate = (E >> 28) & 1
   BAR = ([B + 0x44 + 0x18*i] << 32) | [B + 0x40 + 0x18*i]

3. ep_id = (A >> ep_idx) & ((1 << ep_id_size) - 1)        // 0 when ep_id_size == 0

4. row = table_offset + ep_id

5. W = [B + 0x2000 + 4*row]

6. x = W & 0x3F;  y = (W >> 6) & 0x3F                     // ← the destination

7. translate == 1  →  A' = (A & ((1 << ep_idx) - 1)) + BAR
   translate == 0  →  A' = A, unchanged

8. (x, y) + A' go to the router. A coordinate off the local mesh cannot terminate locally,
   so the packet is forced onto the D2D serial link.
```

Worked example, `A = 0x10000100000`, `B = 0x12230000`:

```
1. entries 0-3 miss;  entry 4: A & ~0x3FFFFFFF = 0x10000000000 == compare   ✓
2. ep_idx 24, ep_id_size 6, table_offset 128, translate 1, BAR 0
3. ep_id = (A >> 24) & 0x3F = 0
4. row = 128
5. [0x12232200] = 0x104
6. (4, 4) in the package frame
7. A' = (A & 0xFFFFFF) + 0 = 0x100000
8. 1 MiB into package (4,4)'s L1
```

A second example showing the **pass-through** case, `A = 0x1002000000000`: entry 5 matches,
`ep_id = (A >> 33) & 0x1F = 16` (the D2D lane bit), row `96 + 16 = 112` → `0x247` → package
`(7,9)`, Mimir m0's second D2D link. `translate = 0`, so the full address is forwarded unchanged
and Mimir's NOC2AXI + SS_BAR performs the final GDDR decode. The programmed `BAR` of
`0x800000000` is never applied.

### 4.2 UMD: (core, offset, size) → flat address

```
1. normalize the frame                        resolve_core()
     LITERAL coordinates pass through; anything else → CoordSystem::NOC0

2. recover the core type
     UNSPECIFIED → read it back from the soc descriptor

3. pick the window from the core type         window_class_for()
     TENSIX, WORKER                                            → WORKER
     DRAM                                                      → DRAM
     ROUTER_ONLY, ARC, PCIE, SECURITY, L2CPU, DISPATCH,
     ETH, ACTIVE_ETH, IDLE_ETH                                 → FULL_TILE
     anything else                                             → throw

4. descriptor frame → package frame:  + package_offset (2,2)

5. guard the 6-bit axis:  package_x < 64 && package_y < 64

6. word = (package_y << 6) | package_x
   selector = selectors_[window_class].at(word)      // inverse precomputed in the ctor

7. guard the slot:  size != 0 && offset < limit && size <= limit - offset
                    where limit = 1 << endpoint_shift

8. return compare | (selector << endpoint_shift) | offset
```

### 4.3 The two are not symmetric

| hardware step | UMD |
|---|---|
| 1. match compares against the address, entry 0 first | **replaced** — selects from `CoreType`; no address is ever matched |
| 2–3. extract `ep_id`, add `table_offset` | inverted implicitly |
| 4–6. row → (x,y) | **the real inverse** — a precomputed `coord → selector` map per window |
| 7. `(A & mask) + BAR` | **not modelled** — see §6.1 |
| 8. route / D2D | out of scope |
| never fails; latches a status bit and misroutes | throws, at four points |

Step 1 is not an inversion at all: hardware asks *"which window does this address fall in?"*, UMD
asks *"which window serves this kind of core?"* — a different question from different input. That
is why `resolve()` needs a `CoreType` argument hardware has no equivalent of.

Step 7 on the UMD side exists **because** hardware has no step 7: a transfer running off the end of
a slot carries into the selector field and silently reaches a different core.

### 4.4 Entry points

```
write_to_device(...) ─┐
                      ├─> noc_{read,write}_translated ─> resolve_core ─> EndpointResolver::resolve
tt_sim_protocol ──────┘                                                      │
   (sim_server client hands the host a coordinate)                           v
                                                       tile_{read,write}_bytes  (coordinate dropped)
                                                           communicator_->global_{read,write}_bytes
```

Two callers, which is why `resolve_core` handles both `LITERAL` and typed coordinates — a client
sends a translated coordinate and the host must resolve it exactly as it resolves its own.

After resolution the coordinate is deliberately discarded: `global_write_bytes` carries no
coordinate, because the simulator has nothing to translate and must not resolve a resolved address
again. The TLB fast path is also bypassed in this mode (`should_use_cached_tlb_window()` returns
`!global_address_mode_ && ...`) — a TLB window is the out-of-band mechanism ATT replaces.

---

## 5. Verification status

**UMD's transcribed map was diffed against the generated SMC boot firmware. All three windows
(`mask`, `ep_idx`, `ep_id_size`, `table_offset`, `translate`) and all 96 endpoint rows match
exactly.** This is a stronger check than the in-repo fixture, which is a transcription of the
*template*; the firmware is the register writes actually performed.

In-repo coverage:

| test | what it pins |
|---|---|
| `test_att_window.cpp` (5) | window bit math, both directions |
| `test_att_resolver.cpp` (11) | frame handling, guards, structural invariants |
| `test_grendel_att_map.cpp` (7) | the qsr.s1 map against `tests/att_maps/grendel_qsr1_att_tables.yaml` |

Two things about that coverage are worth knowing:

- **`MatchesTheTablesTheFirmwareProgrammes` matches windows by `compare` value, not by array
  slot.** Reordering `MapData::windows` would still pass it, and so would
  `EveryEndpointRowResolvesBackToItsOwnSlot`, which is an internal-consistency check. The role →
  window binding is pinned **only** by three literal expected addresses
  (`0x10000100000`, `0x19d8000000`, `0x1000000000000`). Worth knowing before anyone folds those
  into a loop.
- The fixture is a **snapshot**. Refreshing it from grendelemulation is what turns a POR move into
  a test failure instead of a misrouted access. Nothing does this automatically.

---

## 6. Gaps and risks

Ordered by what a planner should care about.

### 6.1 UMD never reads the ATT from hardware

There is no MMIO read of `0x0201_0000` or `0x1223_0000` anywhere. `GRENDEL_QSR1_MAP` is a
`constexpr` struct compiled into the binary, selected by the `TT_UMD_NOC_ATT` environment variable
(deliberately mirroring tt-metal's `TT_METAL_NOC_ATT`):

```cpp
const std::optional<std::string> map_name = utils::get_env_var_value("TT_UMD_NOC_ATT");
if (!map_name.has_value()) { return; }
UMD_ASSERT(map_name == "grendel_qsr1", ...);
noc_address_resolver_ = std::make_unique<att::EndpointResolver>(att::GRENDEL_QSR1_MAP);
global_address_mode_ = true;
```

This is structural, not an oversight: reading the ATT registers requires already being able to
address a tile, which on a global-address part means knowing the ATT.

**Risk:** UMD and firmware can disagree silently. Detection is manual (refresh the fixture).

### 6.2 `BAR` is not modelled

`att::Window` has no `bar` field — confirmed absent from `device/api/umd/device/coordinates/att/`
on `main`. This is safe on qsr.s1 **only because of the values**: both translating windows UMD uses
(entries 4 and 14) have `BAR = 0`, and entry 5's non-zero `BAR` never fires because Quasar programs
it pass-through.

But **entry 8 is programmed `translate = 1` with `BAR = 0x2000000000000`**. UMD does not resolve
through entry 8, so it does not bite today — but translate-with-a-live-BAR is a real configuration
on this silicon. A window UMD resolves through that ever acquires one would need
`local = translated - BAR`, and there is nowhere to put it.

**Risk:** latent, silent, and offset-shifting if it ever lands. Cheap to close now.

### 6.3 `translate_address` is carried but never read

Verified on `main`: the field is declared, assigned in the configs and the test fixtures, and
asserted against the yaml by the fidelity test. **No code consults it.** It is documentation and a
snapshot-fidelity field. Fine, but not obvious from reading the struct.

### 6.4 The forward direction is test-only

`matches()`, `selector()`, `endpoint_index()`, `local_address()` have **zero production call sites**
on `main`; `make_address()`, `transfer_supported()`, `local_address_limit()` and `selector_limit()`
are the ones the resolver uses. The forward path exists as an executable specification that lets
the round-trip test prove the reverse direction. By contrast tt-metal uses the forward direction in
production (§8), so any address → window capability in UMD is unbuilt.

### 6.5 The role → window binding is hardcoded, and the type is the wrong carrier

Hardware stores **no role field** — a mask entry is purely mechanical. The semantics come from the
generator's report (§3.4 label column), were transcribed by hand into the three `WindowClass`
slots, and are mapped from `CoreType` by a hand-written switch.

Two specific exposures:

- **A `CoreType` mapped to the wrong window is silent.** It would resolve to a real coordinate
  through the wrong aperture. A `CoreType` *missing* from the switch is safe — `default:
  UMD_THROW`, deliberately enumerated rather than defaulted. That default is doing most of the
  safety work.
- **`resolve_core` derives the type from the descriptor when `UNSPECIFIED`**, which looks like the
  system knowing the answer but is answering a different question. A NEO tile's descriptor type is
  `TENSIX`, so the derived path always yields L1 and **can never yield that tile's config
  aperture**. The capability does not error; it is just unreachable. Callers must pass a core type
  that is false about the core to get there — which the tests do.

### 6.6 Coordinate-only resolution is impossible on this map

Relevant to any proposal to drop the aperture argument. A single global inverse over the whole
endpoint table gives:

```
distinct coordinates     : 68
  resolve to exactly ONE : 36   (DRAM 8, FULL_TILE perimeter 28)
  ambiguous              : 32   (all WORKER / FULL_TILE)
```

`WORKER` is a **complete subset** of `FULL_TILE` — all 32 NEO tiles appear in both tables, at the
*same selector*, differing only in window base. The table is not deficient; it correctly records
that a tile has two apertures. The missing information is which aperture *this access* wants, which
is a property of the request and was never in the table.

Nothing else breaks the tie: both windows have `translate = 1, BAR = 0`, both slots start at offset
0, and the endpoint row carries no tag.

### 6.7 Three arrays where hardware has one

`MapData` carries `endpoint_words[WORKER|DRAM|FULL_TILE]` — three private slices of what is, in
silicon, one shared pool (§3.4). The comment claims the arrays are *"laid out as the hardware
register file holds it"*, which is true row-for-row inside each slice but oversells the fidelity.

The consequence that matters for planning: **slicing the table into per-role arrays requires
knowing the roles.** You cannot write `endpoint_words[WORKER]` without knowing which rows belong to
the worker window — so the current shape cannot represent an unlabelled map at all. A single
absolute-indexed list could be transcribed verbatim from a register dump with zero semantic
knowledge, with labels becoming an optional layer on top.

Secondary: nested sharing (entry 3's rows 96–111 inside entry 5's 96–127) is awkward, because
`Table<T>` deduces `count` from the array extent and has no `{ptr, count}` constructor.

---

## 7. Sources of truth

| source | status |
|---|---|
| `firmware/quasar/smc/qsr1_boot/src/att_firmware_data.c` (generated) | **authoritative** — the register writes actually performed |
| `models/qsr.s1/att/grendel_address_map_template.yaml` | the generator's input; disagrees with the firmware on entry 5's `translate_addr` (template says 1, programmed is 0) |
| `models/qsr.s1/att/address_map.md`, mask-table section | current; all 14 rows agree with the firmware. The **label column is the only naming authority** |
| `models/qsr.s1/att/address_map.csv` expanded ranges | generated from a deliberately frozen base — **not a source** |
| `docs/GLOBAL_ADDRESSING_COMPONENTS.md` | §3.2/§3.5 field semantics and the RTL formula are good; **stale in two ways** — see below |
| `models/qsr.s1/docs/qmk_att_dump.md` | renders the generator's *intended* values, **not a hardware readback** |

Two corrections worth pushing upstream:

1. **`GLOBAL_ADDRESSING_COMPONENTS.md` is wrong on `table_offset`.** It states large-DDR uses
   `table_offset 120` and that *"endpoint index 96 is unused by the qsr.s1 masks"*. The firmware
   programs **96**, and entries 3 and 5 both use it.
2. **Its endpoint coordinates are in the pre-shift frame** — it lists the first NEO L1 slot as
   `(2,2)` and m0 D2D ingress as `(4,7)/(5,7)`. Post-shift those are `(4,4)` and `(6,9)/(7,9)`,
   i.e. UMD's `0x104` and `0x246`/`0x247`. The doc labels the frame "package frame" but predates
   `quasar_origin` moving to `(3,3)`.

And one in our own tree: `grendel_qsr1_att_map.hpp` says the values were *"Verified against the
live register dump in models/qsr.s1/docs/qmk_att_dump.md."* That file is not a readback, and there
is no readback path in the testbench. The cross-check is real and worth having, but it is
generator-vs-transcription, not silicon-vs-transcription.

---

## 8. Relationship to tt-metal

Vuk Vukomanovic's ATT layer (four commits, 2026-08-27) lives in
`tt_metal/hw/inc/internal/tt-2xx/quasar/noc/att/`. It solves the same problem for device-side
kernels and arrives at a different shape.

**How it selects the window:** an explicit intent enum, not a core type.

```cpp
enum class Kind : std::uint8_t { Local, Worker, Dram, Dispatch, LoopbackScratch };

Address::worker(x, y, offset).encode<MAP>(size)
Address::dram(logical_bank, offset).encode<MAP>(size)
```

The constructor is private; factories are the only way in. Each kind carries its **own identity
type** — workers by coordinate, DRAM by logical bank, dispatch by a table row that carries its own
`WindowClass`. Roles a map lacks are parked on a `NO_WINDOW` sentinel whose `compare` cannot match
any real address, so they fail validation naturally.

| | UMD | tt-metal |
|---|---|---|
| aperture key | `CoreType` (descriptor taxonomy) | `Address::Kind` (explicit intent) |
| DRAM identity | **coordinate** — reaches both D2D lanes | **logical bank** — one selector per bank |
| DRAM on qsr.s1 | populated, 8 rows | **`dram_selectors = {}` — no binding** |
| arbitrary tile config | yes, all 60 | **no `Kind::FullTile`** — self or dispatch only |
| forward direction | test-only | production (`transfer_supported`, `is_self_address`, `extract_local_address`) |
| failure mode | throws | returns `std::optional` |
| evaluation | runtime, `std::unordered_map` | compile-time, `template <const MapData& Map>` |
| multicast | none | `make_worker_multicast` + descriptor pack/unpack |
| endpoint storage | 3 arrays | **4 representations**, workers stored in both directions |

Where **UMD leads**: coordinate-keyed DRAM can express a Mimir's two D2D lanes; metal's
bank-keyed `Table<uint8_t>` cannot, and will need rework when its binding is filled in. UMD can also
address all 60 tiles' config apertures.

Where **metal leads**: the intent enum avoids lying about core types, roles live in data rather
than a switch, and the forward direction is actually used.

Both hit the `WORKER ⊂ FULL_TILE` wall (§6.6). Metal sidesteps it by not offering arbitrary-tile
config addressing and by resolving its own identity worker-first in `resolve_current()`.

**A cross-check exists and is stale.** `tests/tt_metal/tt_metal/noc/test_quasar_att_umd_agreement.cpp`
on branch `pjanevski/att-umd-agreement` compares both sides' flat addresses for every worker and
every perimeter tile, and asserts metal's empty DRAM binding as a tripwire. It constructs
`tt::umd::att::Resolver` — the pre-rename name — so it **will not compile against current UMD
`main`**, where the class is `EndpointResolver`. It is the only thing tying the two transcriptions
together.

---

## 9. Roadmap candidates

Not a plan, and not sized with any confidence beyond shape. Ordered by value per unit of risk.

**Small, closes real exposure**

1. **Fix the stale cross-check test** (`Resolver` → `EndpointResolver`) and get it running in CI.
   It is the only guard against UMD and metal drifting apart, and it is currently dead.
2. **Correct the provenance comment** in `grendel_qsr1_att_map.hpp` (§7) and the ambiguity
   rationale in `att_resolver.hpp`, which blames D2D overlap that does not exist — the real
   constraint is `WORKER ⊂ FULL_TILE`.
3. **Push the two `GLOBAL_ADDRESSING_COMPONENTS.md` corrections upstream** (§7) before someone
   uses that doc to "fix" a correct UMD map.
4. **Add `bar` to `Window`** and either apply it or assert it is zero on any window UMD resolves
   through (§6.2). Cheap insurance against a silent offset shift.

**Medium, unblocks the next product**

5. **One endpoint list instead of three** (§6.7). The highest-value structural item, because it is
   what makes an unlabelled map transcribable at all. Shape: one `Table<uint16_t>` of absolute rows
   in `MapData`, `endpoint_table_offset` stays on `Window` (already there), per-window inverses
   become a derived cache built in the constructor. `resolve()` itself does not change.
6. **Role as data, and a numbered `resolve through window N` primitive** (§6.5), with the
   role-based call layered over it. Removes the `CoreType`-enum coupling and lets a new model
   express a different mapping by transcribing data rather than editing a switch.
7. **An ambiguity diagnostic at construction** — report which coordinates in a map are ambiguous
   and between which windows. Validates a new transcription, supports role discovery on an
   unlabelled map, and flags the day a product change makes something ambiguous that was not.
   Same computation as §6.6, used as a check rather than a resolution shortcut.

**Larger, needs its own design**

8. **An explicit aperture type** replacing `CoreType` at the resolver boundary, so callers stop
   having to misdescribe a core to select an aperture (§6.5). Changes a public signature.
9. **Unification with tt-metal.** The two transcriptions of the same tables will drift. Options
   range from "keep both, keep the cross-check green" to a shared header. Note this is a *design*
   conversation, not a renaming one — `WindowClass` and the `Window` field names can keep UMD's
   spelling under any of them.

**Open questions for planning**

- Does any consumer need an **address → window** capability in UMD (§6.4)? Metal has one; UMD's
  forward methods are built but unused. If yes, it is nearly free. If no, consider saying so in
  the header so the next reader does not assume it is load-bearing.
- Is **arbitrary-tile config access** a requirement, or an accident of UMD's shape? Metal does not
  offer it. If it is required, §6.5's derivation gap means it is currently only reachable by
  passing a false core type.
- Who owns **refreshing the fixture** when POR moves (§5)? Today nothing does it on a schedule.

---

## 10. Where the code is

| path | role |
|---|---|
| `device/api/umd/device/coordinates/att/att_window.hpp` | window bit math, both directions |
| `device/api/umd/device/coordinates/att/att_map.hpp` | `Table<T>`, `WindowClass`, `MapData` |
| `device/api/umd/device/coordinates/att/att_resolver.hpp` | `EndpointResolver`, `resolve_core` |
| `device/coordinates/att/att_resolver.cpp` | resolution, `window_class_for`, the guards |
| `device/api/umd/device/coordinates/att/configs/grendel_qsr1_att_map.hpp` | the qsr.s1 map data |
| `device/tt_device/simulation_tt_device.cpp` | where `resolve_core` is called |
| `device/tt_device/rtl_simulation_tt_device.cpp` | `TT_UMD_NOC_ATT` selection, global-address mode |
| `tests/att_maps/grendel_qsr1_att_tables.yaml` | the fidelity snapshot |
| `tests/baremetal/test_att_*.cpp`, `test_grendel_att_map.cpp` | 23 tests |
