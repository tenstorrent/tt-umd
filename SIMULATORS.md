# Simulators in UMD

> How UMD drives a Tenstorrent device **simulator** instead of real silicon.
> Read top-to-bottom — each section is a talking point.
> Grounded in `main`; class names link to source.

---

## 1. Overview — which simulators do we have?

UMD can talk to a chip that doesn't physically exist. There are **two device backends** behind the
common [`TTDevice`][ttdevice] interface, so the rest of UMD (Cluster, Chip, TLBs, sysmem) barely
knows which one is running:

| | **TTSim** | **External simulators** |
|---|---|---|
| UMD device class | [`TTSimTTDevice`][ttsimdev] | [`RtlSimulationTTDevice`][rtldev] |
| What it is | A C functional model compiled to `libttsim.so` | A separate simulator **process** |
| Where it runs | **In-process** — `dlopen`'d into UMD | **Separate process** (often a **remote** machine) |
| How UMD talks to it | Direct C function calls (function pointers) | **nng** socket + **FlatBuffers** messages |
| Identified by path | a **`.so`** file | a **directory** (has `run.sh` + `soc_descriptor.yaml`) |
| Lives in | this repo (`device/simulation`) | external repo [`tensix/tt-umd-simulators`][sim-repo] |

The external repo ships **two relevant flavors**, both speaking the same nng + FlatBuffers protocol,
so from UMD's side they are one code path ([`RtlSimulationTTDevice`][rtldev]):

| Backend | What it is | Speed | Architectures |
|---|---|---|---|
| **VCS** | **RTL** simulation (Verilog, in VCS) | Slow | Blackhole, Quasar, BOS_A0, Quasar-1x3/2x3 |
| **Emulation (ZEBU)** | Hardware-based emulation | **Much faster** than RTL sim — good for longer tests | Quasar-1x3, Quasar-2x3 |

The single entry point is the factory [`simulation_device_factory.cpp`][factory], which picks the
backend purely from the path in `TT_UMD_SIMULATOR` (or `TT_METAL_SIMULATOR`):

```
  TT_UMD_SIMULATOR=<path>
  create_simulation_tt_device(path)
              |
        path ends in ".so" ?
         /                 \
       yes                  no (a directory)
        |                    |
  TTSimTTDevice        RtlSimulationTTDevice
  (in-process           (external process, nng)
   libttsim.so)          |          \
        |                VCS (RTL)   Emulation (ZEBU)
        |                    |          /
        \___________________ | ________/
                             |
              Cluster / Chip / rest of UMD
```

Both are wrapped by chip classes — [`SimulationChip`][simchip] (base) →
[`TTSimChip`][ttsimchip] / [`RtlSimulationChip`][rtlchip] — and constructed by [`Cluster`][cluster]
under `ChipType::SIMULATION`, so callers (and tt-metal) just see an ordinary cluster of chips.

---

## 2. How UMD talks to a simulator (the shared picture)

Every backend implements the same [`TTDevice`][ttdevice] primitives:

- `read_from_device` / `write_to_device` — NOC tile I/O
- `assert_risc_reset` / `deassert_risc_reset` — core flow control (in/out of reset)
- sysmem DMA callbacks — the device reaching back into **host** memory

Each backend has a **communicator** that turns those into transport-specific operations:

```
  UMD process
  +-----------------------------------------+
  |  TTDevice API  (read / write / reset)   |
  |        |                    |           |
  |  TTSimCommunicator   RtlSimCommunicator |
  +--------|--------------------|-----------+
           |                    |
   in-process C calls    nng + FlatBuffers
           |                    |
           v                    v
      libttsim.so      external sim process
                       (VCS / ZEBU, often remote)
```

> **TTSim is a function call. The external sims are a network message to another process.**
> That one fact is why the external path is more complicated: a process to launch (sometimes on a
> remote machine), a socket to establish, a wire format, and asynchrony to manage.

---

## 3. TTSim (functional simulator, in-process)

### 3.1 What it is
`libttsim.so` is a functional model exposing a flat C ABI (`libttsim_init`, `libttsim_tile_rd_bytes`,
`libttsim_clock`, …). UMD loads it into its own address space and calls it like a library — no
sockets, no serialization. **This is our day-to-day simulator.**

### 3.2 The classes
- [`TTSimTTDevice`][ttsimdev] — the `TTDevice` implementation. Owns the communicator, sysmem manager,
  TLB allocator, and a cached TLB window.
- [`TTSimCommunicator`][ttsimcomm] — thin wrapper over `libttsim.so`: `dlopen`/`dlsym`, resolves
  function pointers, serializes calls under a lock, handles multichip device selection.

### 3.3 Communication — an in-process call

```
  Caller        TTSimTTDevice        TTSimCommunicator      libttsim.so
    |  read_from_device   |                  |                  |
    |-------------------->| lock device_lock |                  |
    |                     | tile_read_bytes  |                  |
    |                     |----------------->| select_device_by_id (multichip)
    |                     |                  | libttsim_tile_rd_bytes
    |                     |                  |----------------->|
    |                     |                  |<-----------------|  bytes
    |                     |<-----------------|  bytes           |
    |                     | advance_clock(1) |  libttsim_clock(1)
    |                     |----------------->|----------------->|
    |<--------------------|  data            |                  |
```

Talking points:
- **No serialization** — a read ends in a direct call to a function pointer inside the loaded `.so`.
- **Clock is manual** — after each access UMD calls `advance_clock(1)` so the model steps forward.
- **DMA / sysmem** — the model can call *back* into host RAM. UMD registers callbacks via
  `set_pcie_dma_mem_callbacks`; when the simulated device does a PCIe NOC access, `libttsim` invokes
  the callback, which reads/writes UMD's sysmem.
- **Multichip** — if the `.so` exports `libttsim_create_device_by_id`/`libttsim_select_device_by_id`,
  one shared `.so` instance backs all chips; each I/O selects its chip under a global lock. Inter-chip
  ethernet links are wired in [`Cluster`][cluster] (the eth-MAC pre-pass) by registering MACs and
  peer handles on each communicator.

### 3.4 Loading modes
- Direct `dlopen` of the `.so`, **or**
- `copy_sim_binary`: copy the `.so` into an in-memory `memfd`, seal it, and `dlopen` that.

### 3.5 Optional knobs
- `TT_SIMULATOR_DRAM_TELEPORT` — fast direct DRAM path (`dram_read_bytes`/`dram_write_bytes`) instead
  of going through tiles, when the model supports it.

### 3.6 TTSim under QEMU — testing the real silicon path
A newer project loads the **TTSim binary into QEMU** so the guest sees a real PCIe device on the bus.
This is powerful because it exercises the **actual silicon code paths** end-to-end (kernel driver,
PCIe enumeration, BAR/TLB mapping, ioctls) rather than UMD's in-process simulation backend — yet it's
backed by the same fast functional model.

```
  QEMU guest
  +-------------------------------+
  |  UMD (silicon path)           |
  |        |                      |
  |  kernel driver / PCIe         |
  +--------|----------------------+
           |  PCIe BARs / config
           v
   QEMU device model  --->  libttsim.so
   (backed by libttsim)
```

Net: it validates the **silicon** path (not just the simulation backend) and is **much faster than
RTL**.

---

## 4. RTL & external simulators (VCS / Emulation) — the complicated one

These run as a **separate process**, talking to UMD over **nng + FlatBuffers**. UMD launches them,
establishes a socket, and exchanges serialized messages. They live in the external repo
[`tensix/tt-umd-simulators`][sim-repo] (build scripts, `run.sh`, `soc_descriptor.yaml`). VCS is the
true **RTL** backend; ZEBU emulation reuses the exact same UMD path.

### 4.1 The classes
- [`RtlSimulationTTDevice`][rtldev] — the `TTDevice` implementation.
- [`RtlSimCommunicator`][rtlcomm] — launches the subprocess (libuv), owns the wire protocol, runs the
  notification thread + command queue.
- [`SimulationHost`][simhost] — the **nng** transport (socket, listener, send/recv).
- [`simulation_device.fbs`][fbs] — FlatBuffers wire schema (`DeviceRequestResponse`, `DEVICE_COMMAND`).
  Mirrors `fbs/tt_simulation_device.fbs` in the simulators repo.

### 4.2 Local vs remote — where the simulator runs
This is the crux of why it's heavy:

- **Local** (Blackhole, Quasar, BOS_A0): simulator runs on the **same** machine. The host spawns it
  via `libuv`; communication is nng (IPC-style, driven by `NNG_SOCKET_NAME`; set
  `TT_SIMULATOR_LOCALHOST=1`).
- **Remote** (Quasar-1x3/2x3, all ZEBU emulation): the run script **SSHs to another machine**
  (`SSH_MACHINE_NAME`, default `soc-l-04`), checks out the *aether* repo, and launches via **LSF
  `bsub`** (VCS) or schedules a **ZEBU** job (emulation). Communication is nng **TCP** via
  `NNG_SOCKET_ADDR` (+ `NNG_SOCKET_LOCAL_PORT` for docker port-forwarding).

UMD's [`SimulationHost`][simhost] is the **server**: it picks a port (`NNG_SOCKET_LOCAL_PORT`, else
random 50000–59999), advertises `tcp://<host>:<port>` via `NNG_SOCKET_ADDR`, then the simulator
process connects back.

Key env vars (UMD side):

| Variable | Purpose |
|---|---|
| `TT_UMD_SIMULATOR` / `TT_METAL_SIMULATOR` | Path to the simulator (`.so` → TTSim; directory → external) |
| `TT_SIMULATOR_LOCALHOST` | Use local host for the nng address |
| `NNG_SOCKET_ADDR` | `tcp://host:port` the simulator connects to (auto-set if unset) |
| `NNG_SOCKET_LOCAL_PORT` | Fixed/forwarded port (else random) |

### 4.3 Bring-up: launch + handshake

```
  RtlSimulationTTDevice   RtlSimCommunicator   SimulationHost     simulator process
                          (nng server)                           (local or remote)
        | initialize()           |                  |                  |
        |----------------------->| init(): open socket, listen         |
        |                        | advertise NNG_SOCKET_ADDR           |
        |                        | uv_spawn <dir>/run.sh (remote: SSH + bsub/ZEBU)
        |                        |------------------------------------>|
        |                        |        connect back (NNG_SOCKET_ADDR)
        |                        |<------------------------------------|
        |                        |   DEVICE_COMMAND_EXIT (readiness ack / handshake)
        |                        |<------------------------------------|
        |                        | start notification thread -> READY  |
```

- **UMD is the server, the simulator is the client.** UMD listens first, advertises the address, then
  runs `<simulator_directory>/run.sh`; the sim connects back.
- **Handshake**: the sim signals readiness by sending an `EXIT` command; UMD blocks until it arrives,
  then starts the notification thread.

### 4.4 The wire protocol (FlatBuffers)
One table carries every request and response ([`simulation_device.fbs`][fbs]):

```flatbuffers
table DeviceRequestResponse {
  command : DEVICE_COMMAND;   // WRITE, READ, *_RESET_*, START, EXIT, AXI_RAM_*_NOTIFICATION, SMN_*
  data    : [uint32];         // write payload, or read response
  core    : tt_vcs_core;      // (x, y)
  address : uint64;
  size    : uint32;
}
```

Selected commands: `WRITE`/`READ` (tile I/O), `ALL_TENSIX_RESET_ASSERT/DEASSERT`, the Quasar
`NEO_DM_*` / `*_UNCORE_*` reset family, `SMN_READ`/`SMN_WRITE` (System NOC), `EXIT` (shutdown +
handshake), and device-initiated `AXI_RAM_READ/WRITE_NOTIFICATION`.

### 4.5 Read round-trip + the async notification thread
The path is asynchronous: a dedicated **notification thread** drains the socket while the calling
thread blocks on a **command queue**. This separates request/response traffic from device-initiated
notifications (the device reaching into host RAM).

```
  Main thread                 Notification thread            simulator process
      | tile_read_bytes(x,y,addr,size)        |                     |
      |--- send READ flatbuffer --------------------------------->  |
      |--- wait_for_command_response() (blocks on CV)               |
      |                            | recv_from_device (5s timeout)  |
      |                            |<--- READ response (data) ------|
      |   <-- push to command_queue, notify CV --|                  |
      |<-- memcpy data, return ----|             |                  |

  meanwhile, device-initiated (device reaches into host RAM):
      |                            |<--- AXI_RAM_READ_NOTIFICATION (addr,size)
      |                            | read host sysmem via callback  |
      |                            |--- echo response with data --->|
```

- **No request IDs** — matching is implicit/ordered: reads send-then-wait, writes are fire-and-forget;
  nng preserves order.
- **Reset commands** carry no data; `assert/deassert_risc_reset` in [`RtlSimulationTTDevice`][rtldev]
  map RISC types to `*_RESET_*` commands (Quasar adds NEO-DM / uncore variants; Wormhole/Blackhole
  use `ALL_TENSIX_*`). **This core flow-control is the part that matters most to UMD here.**

### 4.6 What the external path does *not* support
Throws on `dma_*`, ARC APB/CSM access, `get_clock`, multicast writes, DRAM retrain; no-ops ARC start
and eth-training waits. **NOC translation is disabled** (`get_noc_translation_enabled()` → `false`).

### 4.7 Running it (from the simulators repo guides)
- Build: `cmake -B build -G Ninja && ninja -C build` → produces `build/vcs-blackhole`,
  `build/emu-quasar-1x3`, … Each output **directory** is what `TT_UMD_SIMULATOR` points at.
- Logs: `vcs_<date>_<NNG_SOCKET_NAME>.log` (VCS), `emu_…` in the working dir.
- Waveforms: VCS → `waves.fsdb`. **Kill VCS with `SIGTERM` (`-15`)** so the waveform is valid
  (`-9` corrupts it).
- See the per-client guides in the repo: `vcs/tt-umd.md`, `emu/tt-umd.md`.

---

## 5. CI

Two independent CI systems, one per backend.

### 5.1 TTSim CI — GitHub Actions (this repo) — *the easy one*
File: [`.github/workflows/run-ttsim-tests.yml`](.github/workflows/run-ttsim-tests.yml). Fast and self-contained.
- **Trigger**: `workflow_dispatch` / `workflow_call` (called from UMD and tt-metal pipelines).
- Builds **tt-metal against the newest UMD** — which also implicitly proves tt-metal still builds on UMD.
- Pulls prebuilt `libttsim_{wh,bh,qsr}.so` from the **`tenstorrent/ttsim` GitHub releases** (version
  pinned in tt-metal's `ttsim.yaml`).
- Stages each as `sim_<arch>/libttsim.so` + a matching `soc_descriptor.yaml`.
- Builds UMD `api_tests` with `-DTT_UMD_BUILD_SIMULATION=ON`.
- Runs gtest fixtures `TestDeviceIOFixture.*` / `TTSimDeviceIOFixture.*` per arch (wh/bh/qsr) via
  `TT_UMD_SIMULATOR`, plus a few tt-metal examples/unit tests via `TT_METAL_SIMULATOR`.
- **1-minute timeouts** on the single-card tests — a timeout means a *sim-setup* problem, not a real
  failure (TTSim is that fast).

### 5.2 RTL CI — GitLab pipelines (simulators repo) — *the heavy one*
Lives in the external repo's `.gitlab-ci.yml` (+ `.gitlab/*.yml`); it drives real simulator backends
and watches **both** tt-metal and tt-umd. Stages: `sync → polling → build → post`.
- A **polling agent** (`scripts/polling_agent.py`) watches GitHub PRs in tt-metal/tt-umd and triggers
  a pipeline per PR (names it `<repo> #<PR>: <title>`); plus scheduled **DAILY** / **WEEKLY** runs and
  manual web/MR runs.
- **Build + test**: builds the simulator(s) and UMD/metal, then runs unit tests against the selected
  targets. The `SIMULATORS` variable selects a subset; the **VCS Quasar** target (`qsr`) is the RTL one.
- **Post → Slack `#tt-rtl-sim-ci`**: a daily test summary acts as the heartbeat; on failure it runs
  **auto-bisect** (`scripts/auto_bisect.sh`) to pin a tt-metal regression to a commit, classifies UMD
  regressions (`build_umd` vs test job), and detects timeout/infra failures.
- Schedules are owner-managed under GitLab **CI/CD → Pipeline Schedules**; troubleshooting steps are in
  the repo's `simulator_ci_playbook.md`.

> Mental model: **TTSim CI = a fast GitHub job that downloads a `.so` and runs gtests.
> RTL CI = a scheduled GitLab system that builds backends, bisects regressions, and pings Slack.**

---

## 6. Use cases — when to reach for which

**TTSim (functional, in-process)** — *our day-to-day*
- **PCIe / low-level primitives** — where TTSim shines. Being in-process and fast, it can simulate
  PCIe primitives far more easily than the external sims.
- Fast software bring-up and CI before silicon/RTL is ready.
- Multichip topology / cluster logic (eth wiring, fabric) without RTL cost.
- High-volume functional tests where timing doesn't matter.
- The **QEMU** variant additionally validates the real silicon driver path, still fast.

**RTL & external sims (VCS / ZEBU)** — *mostly an OPs / tt-metal tool*
- Our role is mostly **infra**: we are the layer that lets tt-metal talk to the sim — we enable it,
  we don't drive it heavily.
- **Not** a good fit for PCIe / low-level primitives: very slow, and the host is often on a **remote
  machine** (SSH/LSF/ZEBU), so high-frequency host↔device traffic is impractical.
- For UMD specifically, the genuinely useful part is **core flow control** — asserting / deasserting
  reset to bring cores in and out of run.
- Still valuable for cycle-accurate, real-RTL fidelity and pre-silicon HW/SW co-design — at OPs
  scale, not as a fast iteration loop.
- **VCS (RTL) vs ZEBU emulation**: emulation is **much faster** than RTL simulation, so it's the
  better choice for **longer tests**; reach for VCS when you specifically need RTL-level fidelity.

Rule of thumb: **TTSim for speed and breadth (ours), the external sims for OPs-scale fidelity** —
where UMD is enabling infra and core reset/flow-control is the part that matters to us.

---

## 7. Where the code & artifacts live

### 7.1 Simulator code in UMD (`main`)
```
device/
├── tt_device/
│   ├── simulation_device_factory.cpp   # .so → TTSim, directory → external (RTL)
│   ├── tt_sim_tt_device.cpp            # TTSimTTDevice   (in-process)
│   └── rtl_simulation_tt_device.cpp    # RtlSimulationTTDevice (external/nng)
├── simulation/
│   ├── simulation_chip.cpp             # SimulationChip (base Chip)
│   ├── tt_sim_chip.cpp                 # TTSimChip
│   ├── rtl_simulation_chip.cpp         # RtlSimulationChip
│   ├── tt_sim_communicator.cpp         # TTSim transport (libttsim.so)
│   ├── rtl_sim_communicator.cpp        # external transport (nng + FlatBuffers)
│   ├── simulation_host.cpp             # nng host (server)
│   └── simulation_device.fbs           # wire schema
├── pcie/
│   ├── tt_sim_tlb_window.cpp  / tt_sim_tlb_handle.cpp
│   └── rtl_sim_tlb_window.cpp / rtl_sim_tlb_handle.cpp
├── chip_helpers/
│   ├── simulation_sysmem_manager.cpp
│   └── simulation_tlb_allocator.cpp
└── api/umd/device/…                    # public headers mirror the above
tests/simulation/                       # simulation device tests
```

### 7.2 On-disk simulator artifact (what `TT_UMD_SIMULATOR` points at)
```
# TTSim — a .so; soc_descriptor.yaml sits beside it
<dir>/libttsim.so
<dir>/soc_descriptor.yaml

# External (VCS / ZEBU emu) — a directory with a launcher
build/vcs-blackhole/
├── run.sh               # launched by UMD via libuv (remote → SSH + bsub/ZEBU)
└── soc_descriptor.yaml
```

### 7.3 The simulators repo ([`tensix/tt-umd-simulators`][sim-repo])
```
tt-umd-simulators/
├── README.md                 # backend + client matrix
├── vcs/      (README, tt-umd.md, tt-metal.md, blackhole/ quasar/ …)   # RTL
├── emu/      (README, tt-umd.md, …, quasar-1x3/ quasar-2x3/)          # ZEBU
├── fbs/      tt_simulation_device.fbs                                  # wire schema
├── scripts/  vcs_run.sh, vcs_setup.sh, …
└── common/   simulation_utils.hpp
```
Prereqs (from their README): on **corp VPN**, with the right group memberships; VCS needs a licensed
machine; CI help in `#tt-rtl-sim-ci`.

---

## 8. File / class index

| Concern | Class | Source |
|---|---|---|
| Backend selection | factory fns | [`simulation_device_factory.cpp`][factory] |
| Cluster integration | `Cluster` | [`cluster.cpp`][cluster] |
| Chip base / derived | `SimulationChip` → `TTSimChip` / `RtlSimulationChip` | [`simulation_chip.cpp`][simchip], [`tt_sim_chip.cpp`][ttsimchip], [`rtl_simulation_chip.cpp`][rtlchip] |
| Base device API | `TTDevice` | [`tt_device.hpp`][ttdevice] |
| TTSim device | `TTSimTTDevice` | [`tt_sim_tt_device.cpp`][ttsimdev] |
| TTSim transport | `TTSimCommunicator` | [`tt_sim_communicator.cpp`][ttsimcomm] |
| External device | `RtlSimulationTTDevice` | [`rtl_simulation_tt_device.cpp`][rtldev] |
| External transport | `RtlSimCommunicator` | [`rtl_sim_communicator.cpp`][rtlcomm] |
| nng host | `SimulationHost` | [`simulation_host.cpp`][simhost] |
| Wire schema | `DeviceRequestResponse` | [`simulation_device.fbs`][fbs] |

[sim-repo]: https://yyz-gitlab.local.tenstorrent.com/tensix/tt-umd-simulators
[factory]: device/tt_device/simulation_device_factory.cpp
[cluster]: device/cluster.cpp
[simchip]: device/simulation/simulation_chip.cpp
[ttsimchip]: device/simulation/tt_sim_chip.cpp
[rtlchip]: device/simulation/rtl_simulation_chip.cpp
[ttdevice]: device/api/umd/device/tt_device/tt_device.hpp
[ttsimdev]: device/tt_device/tt_sim_tt_device.cpp
[ttsimcomm]: device/simulation/tt_sim_communicator.cpp
[rtldev]: device/tt_device/rtl_simulation_tt_device.cpp
[rtlcomm]: device/simulation/rtl_sim_communicator.cpp
[simhost]: device/simulation/simulation_host.cpp
[fbs]: device/simulation/simulation_device.fbs
