#!/bin/bash
# Runs a command inside a QEMU VM in which a libttsim.so is attached as a PCIe device and tt-kmd is
# loaded, so the command sees /dev/tenstorrent/N as on silicon. The current directory is shared into
# the guest at the same path and the command runs there. Returns the command's exit code. See
# docs/TTSIM_QEMU.md.
#
# Usage: run-in-ttsim-vm --lib <libttsim.so> --bar4-size <size>
#            [--bar0-size <size>] [--kmd-module <path>] [--pre-kmd <command>] -- <command> [args...]
#   --bar4-size   BAR4 size of the simulated chip: 32M for Wormhole, 32G for Blackhole.
#   --bar0-size   BAR0 size, when what the device advertises by default is not enough. Keraunos
#                 needs 4G: tt-kmd's own TLB window is entry 164, which sits 2.56 GB into BAR0.
#   --kmd-module  tt-kmd to load instead of the released build, for an architecture no release
#                 knows about yet.
#   --pre-kmd     Command to run in the guest before tt-kmd is loaded, for bring-up that a simulated
#                 chip boots without. It runs in the shared working directory.
#
# Example, from a directory holding libttsim_wh.so:
#   docker run --rm -v "$PWD:$PWD" -w "$PWD" \
#       ghcr.io/tenstorrent/tt-umd/tt-umd-ci-ttsim-qemu:latest \
#       run-in-ttsim-vm --lib "$PWD/libttsim_wh.so" --bar4-size 32M -- ls /dev/tenstorrent
set -euo pipefail

QEMU=/opt/ttsim-qemu/bin/qemu-system-x86_64
VM_DIR=/opt/ttsim-vm

usage() {
    sed -n 's/^# \?//; 7,15p' "$0" >&2
    exit 2
}

lib=""
bar4_size=""
bar0_size=""
kmd_module=""
pre_kmd=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --lib) lib="$2"; shift 2 ;;
        --bar4-size) bar4_size="$2"; shift 2 ;;
        --bar0-size) bar0_size="$2"; shift 2 ;;
        --kmd-module) kmd_module="$2"; shift 2 ;;
        --pre-kmd) pre_kmd="$2"; shift 2 ;;
        --) shift; break ;;
        *) usage ;;
    esac
done
[[ -n "$lib" && -n "$bar4_size" && $# -gt 0 ]] || usage

# The guest init reads the command and working directory from this share and writes the exit code back.
control=$(mktemp -d)
trap 'rm -rf "$control"' EXIT
pwd > "$control/workdir"
printf '%q ' "$@" > "$control/cmd"
# Both are optional: the init treats an absent or empty file as "nothing to do".
printf '%s' "$kmd_module" > "$control/kmd-module"
printf '%s' "$pre_kmd" > "$control/pre-kmd"

# BAR0 is a fixed size in the device unless asked for otherwise, so the option is only passed when set.
ttsim_device="ttsim,lib=$lib,bar4-size=$bar4_size"
[[ -n "$bar0_size" ]] && ttsim_device="$ttsim_device,bar0-size=$bar0_size"

# The default 64-bit PCI window cannot fit a Blackhole BAR4. 64G covers its 32G BAR4 plus the other
# BARs at their natural alignment.
# TCG rather than KVM: every BAR access is emulated MMIO forwarded to libttsim, and KVM cannot emulate
# the AVX2 loads and stores UMD uses for device copies, so the guest would get SIGILL.
"$QEMU" \
    -machine q35,accel=tcg -cpu max -smp 4 -m 4G \
    -global q35-pcihost.pci-hole64-size=64G \
    -kernel "$VM_DIR/vmlinuz" -initrd "$VM_DIR/rootfs.cpio.gz" \
    -append "console=ttyS0 loglevel=4 panic=-1" \
    -nographic -no-reboot \
    -virtfs "local,path=$control,mount_tag=ttsim-vm,security_model=none" \
    -virtfs "local,path=$PWD,mount_tag=workdir,security_model=none" \
    -device "$ttsim_device"

[[ -f "$control/exit-code" ]] || { echo "run-in-ttsim-vm: the VM stopped without reporting an exit code" >&2; exit 1; }
exit "$(cat "$control/exit-code")"
