# ttsim in a QEMU VM

UMD's direct simulation path loads a [ttsim](https://github.com/tenstorrent/ttsim) `libttsim.so` into
the process. With [ttsim-qemu](https://github.com/tenstorrent/ttsim-qemu), Tenstorrent's QEMU fork
with a `ttsim` PCI device, the simulator instead shows up as a real PCIe device inside a small Linux
VM. tt-kmd binds to it and creates `/dev/tenstorrent/N`, so UMD and anything built on it run their
unmodified **silicon** path (PCI discovery, KMD ioctls, TLB windows) against the simulator.

The `tt-umd-ci-ttsim-qemu` image packages everything needed: QEMU, a guest kernel with tt-kmd, and
the `run-in-ttsim-vm` runner. CI uses the same runner to test UMD this way.

## Running a command in the VM

`run-in-ttsim-vm` boots the VM with one simulated chip, shares the current directory into the guest
at the same path, runs the command there and returns its exit code:

```bash
run-in-ttsim-vm --lib <libttsim.so> --bar4-size <size> -- <command> [args...]
```

`--bar4-size` is the chip's BAR4 size: `32M` for Wormhole, `32G` for Blackhole and Quasar.
The Quasar model's own BAR4 is 512 GiB, but that collides with tt-kmd's mmap layout: tt-kmd serves an
uncached BAR4 mapping from offset `4<<36` for the BAR's whole length, so a BAR4 over 64 GiB overlaps the
write-combined BAR4 mapping at `5<<36`, and one over 128 GiB the TLB windows at `6<<36`. A TLB mmap that
lands in BAR4 aborts the model with `tlb_in: entry=336` -- an entry at or above 320 is the tell.

The runner gives the guest a fixed 64 GiB 64-bit PCI hole, which holds a 32 GiB BAR4 next to the other
BARs at their natural alignment. A BAR4 over 32 GiB needs a bigger hole and a wider guest physical
address width: firmware leaves a BAR that does not fit unassigned with no error, and the guest simply
has no `resource4`.

Two further options exist for Quasar, and are described under Grendel below: `--bar0-size` and
`--kmd-module`.

The simulator is never part of the image; download the one you need from the
[ttsim releases](https://github.com/tenstorrent/ttsim/releases) and pass it in. For example, to open
a simulated N150 with UMD's own tests, from a UMD checkout with a `build/` directory:

```bash
curl -L -o libttsim_wh.so https://github.com/tenstorrent/ttsim/releases/latest/download/libttsim_wh.so

docker run --rm -v "$PWD:$PWD" -w "$PWD" ghcr.io/tenstorrent/tt-umd/tt-umd-ci-ttsim-qemu:latest \
    run-in-ttsim-vm --lib "$PWD/libttsim_wh.so" --bar4-size 32M -- \
    env LD_LIBRARY_PATH=build/lib build/test/umd/api/api_tests --gtest_filter=ApiClusterTest.OpenAllSiliconChips
```

Use `libttsim_bh.so` with `--bar4-size 32G` for a P150.

## Grendel

A Grendel function needs two things a Wormhole or Blackhole one does not, and the runner takes an
option for each:

| Option | Why |
| --- | --- |
| `--bar0-size 4G` | tt-kmd reserves inbound TLB entry 164 for its own kernel window, which sits `164 << 24` = 2.56 GB into BAR0. At the default size the driver still binds, then logs `BAR0 (0x20000000) too small for AppIn0 entry 164 window; NOC access disabled`: NOC reads and writes then fail with `ENODEV`, and no TLB window can be allocated. |
| `--kmd-module /opt/tenstorrent-keraunos.ko` | No tt-kmd release knows Keraunos yet, so the image carries a second module built from an unreleased commit. The released driver stays the default for every other card. The path is inside the guest; a module on the host works only under the shared working directory, which the guest sees at the same path. |

```bash
curl -L -o libttsim_qsr.so https://github.com/tenstorrent/ttsim/releases/latest/download/libttsim_qsr.so

docker run --rm -v "$PWD:$PWD" -w "$PWD" ghcr.io/tenstorrent/tt-umd/tt-umd-ci-ttsim-qemu:latest \
    run-in-ttsim-vm --lib "$PWD/libttsim_qsr.so" --bar4-size 32G --bar0-size 4G \
    --kmd-module /opt/tenstorrent-keraunos.ko -- \
    env LD_LIBRARY_PATH=build/lib build/test/umd/api/api_tests \
    --gtest_filter='Grendel*:*Quasar*:*QuasarIoWindow*'
```

That filter selects every Grendel and Quasar suite in `api_tests`, so a new one runs without a CI
edit. `*QuasarIoWindow*`, Quasar's TLB window suites, is named on its own so CI fails if they stop
being built. The other TLB suites do not apply here: `TestTlb` needs a `Cluster`, which has no Quasar
topology discovery yet, and the `baremetal_tests` ones touch no device.

A filter that selects nothing still exits 0, so CI runs it through `.github/run-gtest-checked.sh`,
which fails the step when a positive pattern selects no test, a negative pattern excludes none, or
the whole filter selects nothing -- otherwise a build missing those suites would report green having
run nothing.

`--bar0-size` needs a `bar0-size` property on ttsim-qemu's `ttsim` device. BAR0 used to be a
compile-time 512 MiB constant there, and simply raising it is not an option because one image serves
every card. The property landed upstream in ttsim-qemu `aa112ebe16`, which is what
`TTSIM_QEMU_COMMIT` pins; it gives BAR0 the treatment BAR4 already has, so BAR0 stays 512 MiB unless
a caller asks for more and the Wormhole and Blackhole rows are unaffected.

A DMA loopback test has to stay out of that row until the model routes the host window. It drives an
address in the host window, for which the model has no outbound translation, and an unmodelled access
is `_Exit(1)` inside libttsim -- which takes QEMU and the guest with it rather than failing the one case.

Binaries run inside an Ubuntu 22.04 guest, so build them on Ubuntu 22.04, for example in the
`tt-umd-ci-ubuntu-22.04` image. The guest's console, including the command's output, goes to stdout.
If the VM stops without running the command, for example because tt-kmd failed to load or bound no
chip, the runner says so and exits with 1.

## Building the image

```bash
docker build -f .github/ttsim-qemu.Dockerfile -t ttsim-qemu .github
```

The Dockerfile pins the ttsim-qemu commit, the tt-kmd tag and the Keraunos tt-kmd commit as build
arguments (`TTSIM_QEMU_COMMIT`, `TT_KMD_TAG`, `TT_KMD_KERAUNOS_COMMIT`).
A weekly workflow (`check-keraunos-kmd-pin.yml`) opens or updates a tracking issue when tt-kmd's
`keraunos` branch head differs from `TT_KMD_KERAUNOS_COMMIT`.

## Performance

QEMU emulates the guest CPU in software (TCG), so it needs no KVM or nested virtualization, and runs
on any x86-64 Linux host. Booting takes about 20 seconds. After that, time is dominated by the
simulator itself, as on the direct path. KVM is not used because it cannot emulate the AVX2 loads and
stores UMD uses for BAR accesses, which the simulated device turns into emulated MMIO.
