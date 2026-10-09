#!/bin/bash
# Runs a command inside a QEMU VM in which a libttsim.so is attached as a PCIe device and tt-kmd is
# loaded, so the command sees /dev/tenstorrent/N as on silicon. The current directory is shared into
# the guest at the same path and the command runs there. Returns the command's exit code. Needs KVM.
#
# Usage: run-in-ttsim-vm --lib <libttsim.so> --bar4-size <size> -- <command> [args...]
#   --bar4-size  BAR4 size of the simulated chip: 32M for Wormhole, 32G for Blackhole.
#
# Example, from a directory holding libttsim_wh.so:
#   docker run --rm --device /dev/kvm -v "$PWD:$PWD" -w "$PWD" \
#       ghcr.io/tenstorrent/tt-umd/tt-umd-ci-ttsim-qemu:latest \
#       run-in-ttsim-vm --lib "$PWD/libttsim_wh.so" --bar4-size 32M -- ls /dev/tenstorrent
set -euo pipefail

QEMU=/opt/ttsim-qemu/bin/qemu-system-x86_64
VM_DIR=/opt/ttsim-vm

usage() {
    sed -n 's/^# \?//; 6,7p' "$0" >&2
    exit 2
}

lib=""
bar4_size=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --lib) lib="$2"; shift 2 ;;
        --bar4-size) bar4_size="$2"; shift 2 ;;
        --) shift; break ;;
        *) usage ;;
    esac
done
[[ -n "$lib" && -n "$bar4_size" && $# -gt 0 ]] || usage
[[ -e /dev/kvm ]] || { echo "run-in-ttsim-vm: /dev/kvm is missing, pass --device /dev/kvm" >&2; exit 1; }

# The guest init reads the command and working directory from this share and writes the exit code back.
control=$(mktemp -d)
trap 'rm -rf "$control"' EXIT
pwd > "$control/workdir"
printf '%q ' "$@" > "$control/cmd"

"$QEMU" \
    -machine q35,accel=kvm -cpu host -smp 4 -m 4G \
    -kernel "$VM_DIR/vmlinuz" -initrd "$VM_DIR/rootfs.cpio.gz" \
    -append "console=ttyS0 loglevel=4 panic=-1" \
    -nographic -no-reboot \
    -virtfs "local,path=$control,mount_tag=ttsim-vm,security_model=none" \
    -virtfs "local,path=$PWD,mount_tag=workdir,security_model=none" \
    -device "ttsim,lib=$lib,bar4-size=$bar4_size"

[[ -f "$control/exit-code" ]] || { echo "run-in-ttsim-vm: the VM stopped before the command finished" >&2; exit 1; }
exit "$(cat "$control/exit-code")"
