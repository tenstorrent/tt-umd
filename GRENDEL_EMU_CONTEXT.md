# Grendel emulation bring-up — working context

Handoff notes from the session that got UMD reading and writing on a real ZeBu Mimir
emulation model. Written 2026-09-03 on host **soc-zebu-01**. This branch exists only to
carry this file; it is not meant to merge.

## Current state

`TTDevice::write_to_device` / `read_from_device` reach a real ZeBu Mimir model through
chippy's `emu_axi` transport. Verified end to end: 2 tests pass, the write is confirmed by
reading it back through a *raw* chippy transport at the resolver-derived flat address (sharing
nothing with UMD's path but the server), and the emulator side logged zero errors.

```
[ RUN      ] EmuTTDevice.WriteLandsAtTheResolvedFlatAddress
[       OK ] ... (319 ms)      <- real emulation round trips
[ RUN      ] EmuTTDevice.ConsecutiveOffsetsAddressDistinctWords
[       OK ] ... (158 ms)
[ SKIPPED  ] EmuTTDevice.DramCoresDoNotAlias     <- needs GDDR bringup, see §5
```

Everything is on **`origin/pjanevski/grendel-emu-integration` @ `8cff44ff`**.

## 1. Paths

| Path | What |
|---|---|
| `/localdev/pjanevski/work/tt-umd` | main UMD clone (`origin` = `git@github.com:tenstorrent/tt-umd.git`) |
| `/localdev/pjanevski/work/wt-stack` | worktree on the integration branch |
| `/localdev/pjanevski/work/build-umd-stack` | its build dir (GRENDEL_JTAG=ON, SIMULATION=ON, TESTS=ON) |
| `/localdev/pjanevski/work/build-umd-stack-off` | control build, GRENDEL_JTAG=OFF |
| `/localdev/pjanevski/work/chippy` | chippy clone (GitLab `syseng-platform/chippy`) |
| `/localdev/pjanevski/work/.local/hwloc-2.11.2` | locally built hwloc (the box has none) |
| **`/proj_soc/user_dev/pjanevski/grendelemulation`** | emulation repo — **must** be on shared storage, see §3 |

`/localdev` is per-host NVMe and does not exist on other hosts. `/proj_syseng/user_dev` is
group `syseng_users`, which this account is not in; `/proj_soc/user_dev` works.

## 2. Building UMD on soc-zebu-01

```bash
unset -f module; unset MODULESHOME MODULEPATH LOADEDMODULES _LMFILES_
source /tools_soc/opensrc/modules/5.5.0/init/bash
module use /tools_soc/tt/Modules/modulefiles
module load gcc/13.2.1 cmake/3.30.3 ninja/1.12.1 python/3.12.10

HW=/localdev/pjanevski/work/.local/hwloc-2.11.2
cmake -S <src> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTT_UMD_BUILD_GRENDEL_JTAG=ON -DTT_UMD_BUILD_SIMULATION=ON -DTT_UMD_BUILD_TESTS=ON \
  -DCPM_chippy_SOURCE=/localdev/pjanevski/work/chippy \
  -DCMAKE_C_FLAGS="-isystem $HW/include" -DCMAKE_CXX_FLAGS="-isystem $HW/include" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$HW/lib -Wl,-rpath,$HW/lib -ldl -lrt" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$HW/lib -Wl,-rpath,$HW/lib"
```

Why each unusual flag:

- **Modules 5.5.0 init, with the inherited module state unset.** The system `module` is 4.5.2
  and cannot read the SoC modulefiles — their `.modulerc` calls `module-hide`, which needs
  Modules >= 5, and it aborts with `invalid command name "module-hide"`.
- **`-ldl -lrt`** is mandatory: glibc here is 2.28 and only folded `dl*`/`shm_*` into libc in
  2.34. Without them `telemetry` fails on `dlopen` and `umd_misc_tests` on `shm_unlink`. UMD's
  CMake omits them because Ubuntu 24.04 CI has glibc 2.39. An old-distro gap, not a defect.
- **hwloc via `-isystem`/`-L`**: no hwloc package, module, or vendor copy exists on this box,
  yet `device/CMakeLists.txt` links it by bare name. Built 2.11.2 from the release tarball.
- **`python/3.12.10`**: `third_party/CMakeLists.txt` adds nanobind *unconditionally* (not gated
  on `TT_UMD_BUILD_PYTHON`), and nanobind needs Python development headers; there is no
  `python3-devel` for the system 3.12.
- **`TT_UMD_BUILD_SIMULATION=ON`** is required for the emu work: `EmuTTDevice` derives from
  `SimulationTTDevice`, whose translation unit only compiles in a simulation build.

**Never pipe `module load` into `grep`/`head`/`tail`.** It runs in a subshell, the environment
changes are silently discarded, and the "Loading:" banner still prints as though it worked.
This bit twice in one session.

Also: `clang-tidy` is absent, so `TT_UMD_ENABLE_CLANG_TIDY=ON` (the default) silently checks
nothing. The emu module set does bring `clang/21.0.0`, so clang-tidy may be reachable that way.

`api_tests` with **no** `--gtest_filter` segfaults immediately on `ApiChipTest.SimpleAPIShowcase`
on this device-less box. Pre-existing, not a regression. Always filter.

## 3. Running the emulation

Three module roots are needed; the chain
`emu/mimir/26ww36 -> emu/tools/26ww33 -> emu/altair/vov -> altair/licenses/fixed -> python/3.12.10`
spans all of them and fails with a confusing "Unable to locate a modulefile" otherwise.

```bash
export GRENDEL_EMU_DIR=/proj_soc/user_dev/pjanevski/grendelemulation
unset -f module; unset MODULESHOME MODULEPATH LOADEDMODULES _LMFILES_
source /tools_soc/opensrc/modules/5.5.0/init/bash
module use /tools_vendor/tt/Modules/modulefiles   # altair/licenses/fixed
module use /tools_soc/tt/Modules/modulefiles      # python/3.12.10
module use /proj_soc/zebu/Modules/modulefiles     # emu/*
cd $GRENDEL_EMU_DIR/models/mimir
source bin/setup_env.sh                           # puts emu/zrun on PATH
emu run -t 300s -- -sv tests/test_sival_server.py
# endpoint appears in $GRENDEL_EMU_DIR/tmp/silval_server_info.json after 20-40 s
TT_UMD_EMU_SERVER=<host>:<port> \
  /localdev/pjanevski/work/build-umd-stack/test/umd/api/api_tests --gtest_filter='EmuTTDevice.*'
```

Clone: `git clone --depth 1 -b main git@yyz-gitlab.local.tenstorrent.com:tensix/soc/grendelemulation.git`
— only ~20 MB; the ZeBu models themselves are external, loaded by the module.

**It must live on shared storage.** The scheduler dispatches the job to a different host
(`rv-zebu-07` in practice), where `/localdev` does not exist. A `/localdev` clone dies with
`zrun.N: No such file or directory` and `rc=215`.

**Size `-t` to the work.** A server-mode run holds its ZeBu module for the *whole* budget no
matter how fast the tests finish — ours passed in 477 ms then idled ~14 minutes. The run always
ends with `Test failed: Emulation timeout expired` and `rc=0`; for a server that is the normal
exit path, not a failure.

**`emu kill` cannot work from this account.** It shells to the emulator host, ours is denied
(`Permission denied (publickey,...)`), so `zebu_kill_server` fails with exit 255 and the module
never clears — while `emu kill` still logs "Kill signal sent". `nc stop <job>` was silent and
ineffective. The `-t` budget is the only reliable release, so pick it deliberately. Do **not**
run the host-wide `ssh <host> zebu_kill_server -s KILL zRci` that `emu kill -n` suggests: other
users share the unit (socinfra held U8.M0/M1/M3 while we had U8.M2).

Minor: pass the **exact** zrun dir to `emu kill` (`.../models/mimir/zrun.2`, not `.../zrun`).
And `emu who` is a live-refreshing monitor that never exits — pipe it through `head` or it eats
your whole timeout.

`setup_env.sh` names **emu/mimir/26ww36**, newer than `validation/ci/zebu_default_models.json`'s
`26ww33`. Trust `setup_env.sh`; the rules doc warns the table drifts.

## 4. What was built

Five commits on top of main + #3311 + #3335 + flat-noc-address:

| Commit | What |
|---|---|
| `1cc3315e` | `EmuTTDevice` (then named `GrendelEmuTTDevice`) — the backend |
| `028dcdfa` | take Mimir's SPA bases from chippy rather than restating them |
| `d6e1af97` | rename to `EmuTTDevice` |
| `b438e711` | **bug fix**: address the chiplet locally, not by package SPA |
| `8cff44ff` | target the SMC on a bare-reset DUT; gate DRAM; share the address windows |

`EmuTTDevice : SimulationTTDevice` implements essentially only two hooks —
`tile_read_bytes`/`tile_write_bytes` — over chippy's `EmuAxiTransport`. The pre-silicon layer
already owns the rest: `SimulationTTDevice::host_read`/`host_write` translate the coordinate and
apply `noc_address_resolver_` (from flat-noc-address) while the `CoreCoord` — and so the
`CoreType` that selects the address window — is still intact.

Also added: `SimulationBackendType::EMU_AXI`, a third `SimulationTTDevice` constructor (model
only: no local simulator, no UMD `SimulationClient`, so `client_mode()` stays false and accesses
take the host path), and an `EMU_AXI` case in `simulation_connector.cpp` that throws, since a UMD
client attaching over that socket has no chippy connection.

The call chain:

```
TTDevice::write_to_device(core, addr)
  -> SimulationTTDevice::host_write: translate coord, then flatten via noc_address_resolver_
  -> EmuTTDevice::tile_write_bytes  -> chippy EmuAxiTransport::write
  -> "WR32,<addr>,<val>" over TCP  -> test_sival_server.py -> tb.axi.write_reg32 -> ZeBu Mimir
```

## 5. Findings that cost real time

**The emu path addresses the chiplet LOCALLY, not by package SPA.** This was a genuine bug in
the first implementation. Evidence: `GrendelAxiEmu` builds its chiplets with
`init_chiplets(metadata, /*use_spa_addressing_for_local_chiplet=*/false, false)`;
`test_sival_server.py` wires the protocol straight to `tb.axi.read_reg32`, the chiplet's own AXI
master; and the model's own `run_cce_via_sival.py` addresses CCE SRAM at its local `0x40000000`
while listing the SPA base `0x1280000000` as a *separate* constant. The package SPA bases
(`kMimir_0_SpaBaseAddr` etc.) apply when a Mimir is reached through the fabric of a larger
package — not this path.

**The mock server structurally cannot catch address-space errors.** chippy's
`lib/transport/emu_axi_transport/emu_server/mock_server.py` hands out a zeroed, *fully writable*
slot for any address its RDL map does not cover, so a completely wrong window round-trips like
RAM. Every test passed against it with the wrong addresses. It is still valuable — same protocol
layer, real reset defaults, per-bit write masks, RO registers that ignore writes, and it models
the SMC alias remap at `0xC000_0000` — but `RUNSIM` is an explicit no-op and there is no RTL,
firmware execution, or side-effecting register behaviour.

**One stray DRAM write wedges the model for the whole session.** The SiVal server runs preload
and reset only (`test_sival_server.py` skips `tb.configure()`), so GDDR has had no bringup. A
DRAM access stalls the testbench AXI master — `axi4m WR_STALL stalled at 0x800000400
(AWREADY=0)` — the server drops the client, and **the stalled transaction outlives the server's
own DUT re-init**: every later access, reads included, reports the same stale stall. Hence
DRAM is opt-in behind `TT_UMD_EMU_DRAM=1`, and the default tests use SMC SRAM at `0x0006_0000`
in the AXI view (`0xC006_0000` is the same memory through the SMC core-local alias, which is
what firmware download and reset vectors use).

**`QUIT` is the orchestrator's to send, never the device's.** chippy's `teardown()` is
QUIT-plus-close, and QUIT ends the session for whoever owns the server: the mock server sets its
shutdown event and a real run ends the emulation job. Ownership sits with
`run_validation_test.py::_send_quit_command`. An early version sent it from the destructor and
the first test killed the server under the next two. `~EmuAxiTransport` is defaulted and there is
no close-without-QUIT, so the socket is released at process exit — a small chippy addition would
fix that properly.

**Mimir needs its own address windows.** `GrendelAddressWindows`' defaults are Quasar-mesh
shaped. For a lone Mimir: config aperture indexed by chiplet instance, the mesh collapsed to 1x1
anchored on the SMC core, and the DRAM stride taken from the descriptor's own `dram_bank_size`.
`validate()` rejects a zero NEO extent, so the NEO grid is parked outside the descriptor's grid.
Exposed as `EmuTTDevice::mimir_address_windows()` so tests assert against the same definition the
device uses — the independent check had been building a resolver with the *default* windows,
where the SMC core falls outside the Quasar mesh and throws.

**Open question for whoever owns the Mimir address map:** the resolver's Quasar `dram_stride`
default (8 GiB) and chippy's `mimir.h` GDDR tile spacing (128 GiB in SPA) disagree by 16x. Which
applies in the local view is unconfirmed; `DramCoresDoNotAlias` is the test that would settle it
on a model with GDDR brought up.

## 6. Branches, PRs, and the restack

| Branch | PR | Contains |
|---|---|---|
| `pjanevski/grendel-jtag-link-chippy` | **#3309** | opt-in chippy build link |
| `pjanevski/grendel-jtag-protocol` | **#3311** | `GrendelJtagProtocol` + 6 mock tests (contains #3309) |
| `pjanevski/soc-desc-smc-core-type` | **#3335** | `CoreType::SMC`, `ARCH::GRENDEL`, single-Mimir descriptor |
| `pjanevski/grendel-flat-noc-address` | **none** | `NocAddressResolver` + the `SimulationTTDevice` hook |
| `pjanevski/grendel-emu-integration` | none | all of the above merged + the 5 emu commits |

**The PRs are not stacked.** Only #3309 -> #3311 is a real stack. #3335 branches from an older
`main` (`cdac091f`) with no relation to the JTAG work, and flat-noc-address is 153 commits behind
with no PR at all despite being load-bearing for the emu path.

Verified combinable: zero file overlap between #3311 and #3335; all merge cleanly with main
`da53e470`. On the integration branch: 10/10 targeted `api_tests`, **265/265 baremetal**
(including 11 `GrendelNocAddressResolver.*`), and the `GRENDEL_JTAG=OFF` build unaffected.

Restack order must put **#3335 before flat-noc and emu** — the emu code needs `ARCH::GRENDEL`, so
cherry-picking onto flat-noc alone will not compile. Target shape:
`#3309 -> #3335 -> flat-noc -> emu`, leaving `#3309 -> #3311` alone since it is already correct.

**Not yet done, and deliberately:** the rebases and force-pushes are blocked on GitHub auth.
`gh` exists at `/tools_vendor/FOSS/gh/2.53.0/bin/gh` but is not authenticated and there is no
token. Rebasing without being able to retarget the PR bases would make each PR's diff show its
parent's commits — worse than the current state. Run `gh auth login` first, then:

```bash
cd /localdev/pjanevski/work/wt-stack
git rebase origin/main pjanevski/grendel-jtag-link-chippy && git push --force-with-lease
git rebase pjanevski/grendel-jtag-link-chippy pjanevski/grendel-jtag-protocol && git push --force-with-lease
git rebase --onto pjanevski/grendel-jtag-link-chippy \
  $(git merge-base origin/main pjanevski/soc-desc-smc-core-type) pjanevski/soc-desc-smc-core-type
git push --force-with-lease && gh pr edit 3335 --base pjanevski/grendel-jtag-link-chippy
git rebase --onto pjanevski/soc-desc-smc-core-type \
  $(git merge-base origin/main pjanevski/grendel-flat-noc-address) pjanevski/grendel-flat-noc-address
git push --force-with-lease
gh pr create --base pjanevski/soc-desc-smc-core-type --head pjanevski/grendel-flat-noc-address
git checkout -b pjanevski/grendel-emu-backend pjanevski/grendel-flat-noc-address
git cherry-pick 1cc3315e 028dcdfa d6e1af97 b438e711 8cff44ff
git push -u origin pjanevski/grendel-emu-backend
gh pr create --base pjanevski/grendel-flat-noc-address --head pjanevski/grendel-emu-backend
```

Review note for #3311: it carries a `.claude/settings.json` with a curl-to-GitHub-API permission
allowlist — local tooling config in a product PR.

## 7. Open items

1. **The `RUNSIM` time hook** — the one real code gap. Emulation advances only when told.
   UMD waits on wall clock: `poll_until` at `device/common/utils.hpp:198` plus ~a dozen bare
   `sleep_for` calls (`tt_device.cpp:486`, `blackhole_device_firmware.cpp:273`,
   `topology_discovery.cpp:690`). chippy's equivalent is
   `utils::Time::set_sleep_func([t](double s){ t->run_sim(s); })` in `system_if.cpp:261-278`.
   The current tests never sleep, so they pass without it — anything that polls (SMC bring-up,
   firmware handshakes) will hang. Now testable for real, since the emulator honours RUNSIM.
2. **Orchestrator flag parsing.** `run_validation_test.py` injects `--interface.type`,
   `--chip.package`, `--interface.parameters.server_ip/_port`; our test reads
   `TT_UMD_EMU_SERVER` and would skip. Teaching it those four flags (~30 min) makes it a
   first-class orchestrated chippy test. `_resolve_under_chippy` passes absolute paths through
   unchanged, so the binary can live anywhere — no symlink needed.
3. **Multi-chiplet.** `EmuTTDevice` holds a bare `EmuAxiTransport` and issues no
   `SELECT_CHIPLET`. Fine for mimir (single-chiplet models ignore it), but `mmk`/`qsr.s1` need
   `GrendelAxiEmu`/`MultiChipletEmuAxiTransport`. Beware the documented false-positive: a
   multi-chiplet factory on a single-chiplet platform enumerates phantom chiplets whose
   SELECT_CHIPLETs are dropped, and every access silently hits the same physical chiplet.
4. **DRAM on a configured model** — settles the stride question in §5.
5. **#3311's protocol is not on this path** at all: `SimulationTTDevice` overrides
   `read_from_device`/`write_to_device` and never consults `get_device_protocol()`.

## 8. Decisions already made — please don't re-litigate

- **The class is `EmuTTDevice`**, named for its backend like `RtlSimulationTTDevice` and
  `TTSimTTDevice` beside it. Not `GrendelEmuTTDevice`, and *not* `GrendelTTDevice` — "Grendel"
  in a device name reads as a claim on the Grendel **silicon** device, which this is not.
- **A generic protocol-driven `GrendelTTDevice` was explicitly rejected.** It was proposed and
  turned down; don't propose it again.
- **The emu backend is a simulation-level backend**, not a silicon device model. A future
  silicon path may use a Grendel model and override the simulation parts lower down.
- **The integration branch is a test vehicle**, on purpose: all four lines of work on one branch
  so everything can be exercised before review. The three merge commits need unpicking (§6)
  before submission.
- **UMD owns core -> flat address**; chippy owns everything below it. `CoreCoord`/`CoreType` are
  UMD concepts — chippy has no coordinate notion for Grendel ("chippy addresses a Mimir chiplet
  flat, with no coordinate component"). chippy's `lib/address_translation` is an RDL
  register/IP layer, not a coordinate translator.
- If the protocol route is ever revisited: **a flat-address protocol needs no interface change**,
  because the existing `DeviceProtocol` signature already carries `addr` and the coordinate is
  simply ignored. The blocker only appears if you try to resolve *inside* the protocol —
  `resolve_coordinate` (`tt_device.cpp:721`) returns a bare `xy_pair`, dropping the `core_type`
  that selects the window. #3311's `SmnTransportProvider` has the same blind spot.
