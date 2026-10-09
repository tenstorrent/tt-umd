#!/bin/bash
# PID 1 of the ttsim VM guest. Runs the command run-in-ttsim-vm.sh left in the control share, stores its
# exit code next to it, and powers the VM off. PID 1 must never exit, or the kernel panics.

power_off() {
    sync
    echo o > /proc/sysrq-trigger
    sleep infinity
}

# Leaving no exit code behind makes the host report a VM failure instead of a command failure.
fail() {
    echo "ttsim-vm-init: $*" >&2
    dmesg | tail -50 >&2
    power_off
}

mount -t proc proc /proc
mount -t sysfs sysfs /sys
# devtmpfs creates /dev/tenstorrent/N on its own, so no udev is needed.
mount -t devtmpfs devtmpfs /dev

modprobe 9pnet_virtio && modprobe 9p || fail "cannot load the 9p modules"
mkdir -p /ttsim-vm
mount -t 9p -o trans=virtio,version=9p2000.L ttsim-vm /ttsim-vm || fail "cannot mount the control share"
# The host working directory is shared at its host path, so commands can use host paths unchanged.
WORKDIR=$(cat /ttsim-vm/workdir)
mkdir -p "$WORKDIR"
mount -t 9p -o trans=virtio,version=9p2000.L,msize=1048576 workdir "$WORKDIR" \
    || fail "cannot mount the working directory"

# Turns off tt-kmd's reset-on-probe recovery for chips that are not ready, which a simulated chip does
# not need. craq-sim's QEMU smoke loads tt-kmd the same way.
insmod /opt/tenstorrent.ko reset_limit=0 || fail "cannot load tt-kmd"

cd "$WORKDIR"
bash /ttsim-vm/cmd
echo $? > /ttsim-vm/exit-code

power_off
