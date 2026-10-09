# Context: ttsim QEMU CI (issue #2852)

Working notes for continuing #2852 on another machine. This branch is the code branch of draft PR #3625 (`pjanevski/ttsim-qemu-ci`) plus this one file. Don't merge it; check it out only to pick up the work.

## Goal

#2852 asks for support for ttsim running through QEMU, reusable by other teams, and for it to run in UMD CI. With [ttsim-qemu](https://github.com/tenstorrent/ttsim-qemu), libttsim is a real PCIe device inside a VM, tt-kmd binds it, and `/dev/tenstorrent/N` appears. UMD inside the guest then takes the full silicon path (PCI discovery, KMD ioctls, TLBs) against the simulator, which today's dlopen-based ttsim CI never exercises.

## Decisions (user-confirmed)

- **Separate thin Docker image**, not inline workflow steps plus `actions/cache`. Other teams get it ready to run (`docker run …`), and `docker-image.yml` already handles content-hashed rebuilds and `:latest`.
- The image lives in tt-umd for now, following the `.github/<variant>.Dockerfile` pattern, and is published as `ghcr.io/tenstorrent/tt-umd/tt-umd-ci-ttsim-qemu`. Moving it to ttsim-qemu later is fine.
- **Assume CI machines expose KVM.** The runner requires `/dev/kvm` and has no silent software-emulation fallback.
- **Only what is needed to run goes into the image.** No `linux-modules-extra` (9p is in base `linux-modules`), and no multi-chip option until a caller needs it.
- **GPL:** libttsim.so is never baked into the image; it is mounted in at runtime. The image's `org.opencontainers.image.source` label points at the public ttsim-qemu commit.
- **PR split:**
  - PR1 (#3625) is the mechanism plus a Wormhole boot smoke.
  - PR2 runs `ApiClusterTest.OpenAllSiliconChips` in the VM on N150, P150, WH galaxy and BH galaxy.

## How PR1 works

| File | Role |
|---|---|
| `.github/ttsim-qemu.Dockerfile` | Multi-stage build; see the stages below |
| `.github/ttsim-vm-init.sh` | Guest PID 1. Mounts proc, sysfs and devtmpfs (devtmpfs creates `/dev/tenstorrent/N`, so no udev), mounts the two 9p shares, loads tt-kmd with `reset_limit=0`, runs the command, writes its exit code, and powers off through sysrq. Any setup failure prints the error and a `dmesg` tail, then powers off without writing an exit code |
| `.github/run-in-ttsim-vm.sh` | Installed as `run-in-ttsim-vm --lib <libttsim.so> --bar4-size <32M\|32G> -- <cmd>`. Shares the control directory and `$PWD` (mounted at the same path in the guest), boots `q35,accel=kvm` with `-kernel`/`-initrd`, and returns the command's exit code |
| `.github/workflows/run-ttsim-qemu-tests.yml` | Separate, non-required workflow. Builds the image (60 min timeout), then on `tt-ubuntu-2204-large-stable` with `--device /dev/kvm` downloads `libttsim_wh.so` and runs `ls -l /dev/tenstorrent/0` in the VM |

The Dockerfile stages:
1. **qemu:** ttsim-qemu at commit `cade2422`, configured with `--target-list=x86_64-softmmu --enable-kvm --enable-virtfs`. It needs `libattr1-dev` for 9p and `python3-tomli` on 22.04.
2. **kmd:** tt-kmd `ttkmd-2.11.0` built against `linux-headers-generic`. The kernel version goes into `/kver`, and the guest stage installs that same version.
3. **guest:** `ubuntu:22.04` plus `linux-image-$KVER`, `kmod`, `tenstorrent.ko`, and the init script as `/init`. Ubuntu 22.04 matches the glibc of the UMD CI build image.
4. **pack:** the guest filesystem as a gzip cpio initramfs, plus `vmlinuz`.
5. **final image:** QEMU runtime libraries, QEMU, the guest, and the runner. It runs `qemu-system-x86_64 --version` at build time to catch a missing library.

## Building and running on a machine with Docker and KVM

```bash
git fetch origin && git checkout pjanevski/ttsim-qemu-ci
docker build -f .github/ttsim-qemu.Dockerfile -t ttsim-qemu .github

# Latest public Wormhole simulator (see the tenstorrent/ttsim releases)
mkdir -p /tmp/ttsim-wh && cd /tmp/ttsim-wh
curl -L -o libttsim_wh.so https://github.com/tenstorrent/ttsim/releases/latest/download/libttsim_wh.so

docker run --rm --device /dev/kvm -v "$PWD:$PWD" -w "$PWD" ttsim-qemu \
    run-in-ttsim-vm --lib "$PWD/libttsim_wh.so" --bar4-size 32M -- ls -l /dev/tenstorrent/0
```

## Untested: likely first failures

PR1 was never built. bgd-lab-06 has no Docker, KVM or sudo. Check these first:

- **Kernel install in the Docker build:** `linux-image-$KVER` postinst steps (initramfs-tools) running inside `docker build`.
- **QEMU build:** QEMU's meson subprojects are downloaded at configure time. QEMU may also link a library the final stage doesn't install; the `--version` step catches that.
- **Guest boot:**
  - the size of the initramfs as a rootfs (4G RAM in the guest);
  - whether `/init` runs;
  - whether the 9p mounts work.
- **tt-kmd:** whether `reset_limit=0` is enough, or tt-kmd needs another parameter. Also whether `/dev/tenstorrent/0` exists right after `insmod`; craq-sim sleeps 1 s before checking.
- **Runners:** whether `tt-ubuntu-2204-large-stable` actually passes `/dev/kvm` into the job container.

## How others run ttsim-qemu (prior art)

- **ttsim-qemu itself has no CI.** Its only workflow, `lockdown.yml`, comes from the upstream QEMU mirror; there are 0 Actions runs ever and 0 checks on head. ttsim and ttsim-private have no QEMU job either.
- **craq-sim's `qemu-pci-smoke`** (in `.github/workflows/build-and-test.yml`, with the harness in `scripts/test_qemu_kmd.py`) is the only automated user:
  - runs on a GitHub-hosted `ubuntu-22.04` runner and rebuilds everything per run;
  - QEMU from the `nkapreTT/ttsim-qemu` fork (waiting on ttsim-qemu PR #1);
  - tt-kmd built against the runner's own kernel, which also serves as the guest kernel;
  - a busybox-static initramfs, with `insmod tenstorrent.ko reset_limit=0`, then a check for `/dev/tenstorrent/N`;
  - `accel=tcg` (no KVM) with a 90 s timeout, covering Wormhole and Blackhole with 2 chips each.

  It shows that software emulation (TCG) is enough for the KMD-bind smoke. KVM matters once UMD tests run inside the guest.
- **ttsim-qemu PR #1** (open) adds the `num-chips=` and `index=` device properties for multi-device setups. The multi-chip QEMU options will likely change when it lands.

## PR2 scope (next)

- Run `ApiClusterTest.OpenAllSiliconChips` inside the VM on N150, P150, WH galaxy and BH galaxy.
- **Multi-chip:** one `-device ttsim` per MMIO chip, designed against ttsim-qemu's current interface (check PR #1 first).
- **Blackhole:** `--bar4-size 32G`. This probably needs a bigger q35 64-bit PCI hole (`-global q35-pcihost.pci-hole64-size=…`) and enough guest physical address bits, especially for galaxy (32 chips).
- **Guest runtime libraries:** the UMD test binaries (from the `tt-umd-ci-ubuntu-22.04` build) may need libraries the minimal guest lacks, such as hwloc.
- **CI:** decide whether to make the workflow required, and whether to run it on every PR now that UMD code is exercised.

## Related

- Umbrella #2679 (simulation support); #2889 (TTSim through the silicon device models over TTSimProtocol).
- The ttsim README section "Running ttsim as a QEMU PCI Device", and `README.ttsim.md` in ttsim-qemu.
