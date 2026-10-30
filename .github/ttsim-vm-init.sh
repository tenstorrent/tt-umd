#!/bin/bash
# PID 1 of the ttsim VM guest. Runs the command run-in-ttsim-vm.sh left in the control share, stores its
# exit code next to it, and powers the VM off. If setup fails, it powers off without an exit code, so the
# host reports a VM failure instead of a command failure. PID 1 must never exit, or the kernel panics.

power_off() {
    sync
    echo o > /proc/sysrq-trigger
    sleep infinity
}

fail() {
    echo "ttsim-vm-init: $*" >&2
    dmesg | tail -50 >&2
    power_off
}

mount -t proc proc /proc
mount -t sysfs sysfs /sys
# devtmpfs creates /dev/tenstorrent/N on its own, so no udev is needed.
mount -t devtmpfs devtmpfs /dev
# UMD keeps its cross-process mutexes in POSIX shared memory.
mkdir -p /dev/shm
mount -t tmpfs tmpfs /dev/shm

modprobe 9pnet_virtio && modprobe 9p || fail "cannot load the 9p modules"
mkdir -p /ttsim-vm
mount -t 9p -o trans=virtio,version=9p2000.L ttsim-vm /ttsim-vm || fail "cannot mount the control share"
# The host working directory is shared at its host path, so commands can use host paths unchanged.
WORKDIR=$(cat /ttsim-vm/workdir)
mkdir -p "$WORKDIR"
mount -t 9p -o trans=virtio,version=9p2000.L,msize=1048576 workdir "$WORKDIR" \
    || fail "cannot mount the working directory"

cd "$WORKDIR"

# Bring-up that a simulated chip boots without: programming the inbound TLB and the address
# translation table, which firmware would have left done on silicon. It runs before tt-kmd claims the
# device, because it reaches those registers through the BARs itself.
if [[ -s /ttsim-vm/pre-kmd ]]; then
    bash /ttsim-vm/pre-kmd || fail "the --pre-kmd command failed"
fi

# Turns off tt-kmd's reset-on-probe recovery for chips that are not ready, which a simulated chip does
# not need. craq-sim's QEMU smoke loads tt-kmd the same way. An architecture no release knows about
# yet names its own module instead of the default build.
kmd=$(cat /ttsim-vm/kmd-module 2>/dev/null)
insmod "${kmd:-/opt/tenstorrent.ko}" reset_limit=0 || fail "cannot load tt-kmd"

bash /ttsim-vm/cmd
echo $? > /ttsim-vm/exit-code

power_off
