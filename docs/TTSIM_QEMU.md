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

`--bar4-size` is the chip's BAR4 size: `32M` for Wormhole, `32G` for Blackhole. Quasar is not
supported, because ttsim does not implement its host MMIO path.

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

Binaries run inside an Ubuntu 22.04 guest, so build them on Ubuntu 22.04, for example in the
`tt-umd-ci-ubuntu-22.04` image. The guest's console, including the command's output, goes to stdout.
If the VM stops without running the command, for example because tt-kmd failed to load, the runner
says so and exits with 1.

## Building the image

```bash
docker build -f .github/ttsim-qemu.Dockerfile -t ttsim-qemu .github
```

The Dockerfile pins the ttsim-qemu commit and the tt-kmd tag as build arguments
(`TTSIM_QEMU_COMMIT`, `TT_KMD_TAG`).

## Performance

QEMU emulates the guest CPU in software (TCG), so it needs no KVM or nested virtualization, and runs
on any x86-64 Linux host. Booting takes about 20 seconds. After that, time is dominated by the
simulator itself, as on the direct path. KVM is not used because it cannot emulate the AVX2 loads and
stores UMD uses for BAR accesses, which the simulated device turns into emulated MMIO.
