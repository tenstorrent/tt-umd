# ttsim-qemu: runs commands inside a QEMU VM in which ttsim is a real PCIe device.
#
# The guest loads tt-kmd and sees /dev/tenstorrent/N, so software in it takes the full silicon path
# (PCI discovery, KMD ioctls, TLBs) against the simulator. See run-in-ttsim-vm.sh for usage.
#
# libttsim.so is deliberately not part of the image: QEMU is GPL and libttsim must stay a separate
# binary the user mounts in at runtime.

ARG UBUNTU_VERSION=22.04
ARG TTSIM_QEMU_COMMIT=cade2422b833f77e5afddcec7da6916993bc2416
ARG TT_KMD_TAG=ttkmd-2.11.0

# Stage 1: QEMU with the ttsim PCI device.
FROM ubuntu:${UBUNTU_VERSION} AS qemu

ARG TTSIM_QEMU_COMMIT
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        bison \
        build-essential \
        ca-certificates \
        flex \
        git \
        libattr1-dev \
        libglib2.0-dev \
        libpixman-1-dev \
        ninja-build \
        pkg-config \
        python3 \
        python3-tomli \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

RUN git init /src && cd /src \
    && git fetch --depth 1 https://github.com/tenstorrent/ttsim-qemu.git ${TTSIM_QEMU_COMMIT} \
    && git checkout FETCH_HEAD \
    && ./configure --target-list=x86_64-softmmu --prefix=/opt/ttsim-qemu \
        --enable-virtfs --disable-docs --disable-werror \
    && make -j"$(nproc)" install \
    && rm -rf /src

# Stage 2: tt-kmd built against the guest kernel. /kver carries the kernel version to the guest stage
# so both install the exact same kernel.
FROM ubuntu:${UBUNTU_VERSION} AS kmd

ARG TT_KMD_TAG
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        git \
        linux-headers-generic \
    && rm -rf /var/lib/apt/lists/*

RUN ls /lib/modules > /kver \
    && git clone --depth 1 --branch ${TT_KMD_TAG} https://github.com/tenstorrent/tt-kmd.git /tt-kmd \
    && make -C /tt-kmd KDIR=/lib/modules/"$(cat /kver)"/build

# Stage 3: the guest root filesystem. It boots as an initramfs, so it needs no disk image, and PID 1
# is ttsim-vm-init.sh instead of an init system.
FROM ubuntu:${UBUNTU_VERSION} AS guest

ENV DEBIAN_FRONTEND=noninteractive

COPY --from=kmd /kver /kver
RUN apt-get update && apt-get install -y --no-install-recommends \
        kmod \
        libhwloc15 \
        linux-image-"$(cat /kver)" \
    && rm -rf /var/lib/apt/lists/*

COPY --from=kmd /tt-kmd/tenstorrent.ko /opt/tenstorrent.ko
COPY ttsim-vm-init.sh /init
RUN chmod +x /init

# Stage 4: pack the guest. The kernel is passed to QEMU separately, so /boot is left out of the
# initramfs.
FROM ubuntu:${UBUNTU_VERSION} AS pack

RUN apt-get update && apt-get install -y --no-install-recommends cpio \
    && rm -rf /var/lib/apt/lists/*

COPY --from=guest / /rootfs
RUN mkdir /out \
    && cp /rootfs/boot/vmlinuz-"$(cat /rootfs/kver)" /out/vmlinuz \
    && rm -rf /rootfs/boot/* \
    && cd /rootfs && find . -xdev | cpio --quiet -o -H newc | gzip -1 > /out/rootfs.cpio.gz

# Final image: QEMU, the guest, and the runner.
FROM ubuntu:${UBUNTU_VERSION}

ARG TTSIM_QEMU_COMMIT
LABEL org.opencontainers.image.description="QEMU VM exposing ttsim as a PCIe device"
LABEL org.opencontainers.image.source="https://github.com/tenstorrent/ttsim-qemu/tree/${TTSIM_QEMU_COMMIT}"

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libattr1 \
        libglib2.0-0 \
        libpixman-1-0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=qemu /opt/ttsim-qemu /opt/ttsim-qemu
# Fails the image build if QEMU needs a runtime library installed above is missing.
RUN /opt/ttsim-qemu/bin/qemu-system-x86_64 --version
COPY --from=pack /out /opt/ttsim-vm
COPY run-in-ttsim-vm.sh /usr/local/bin/run-in-ttsim-vm
RUN chmod +x /usr/local/bin/run-in-ttsim-vm
