# ttsim-qemu: runs commands inside a QEMU VM in which ttsim is a real PCIe device.
#
# The guest loads tt-kmd and sees /dev/tenstorrent/N, so software in it takes the full silicon path
# (PCI discovery, KMD ioctls, TLBs) against the simulator. See docs/TTSIM_QEMU.md for usage.
#
# libttsim.so is deliberately not part of the image: QEMU is GPL and libttsim must stay a separate
# binary the user mounts in at runtime.
#
# Stages 1-4 only produce build outputs and are discarded. The final stage, the only one published,
# copies in just those outputs, so toolchains, sources and headers stay out of the shipped image.

ARG UBUNTU_VERSION=22.04
# Pinned to the commit that gives BAR0 a bar0-size property. Keraunos needs a 4 GiB BAR0 -- tt-kmd's
# own TLB window is inbound entry 164, which sits 2.56 GB in -- and before this commit the ttsim
# device fixed BAR0 at a compile-time 512 MiB. The property defaults to that old 512 MiB, so the N150
# and P150 rows that share this image are unaffected. It lives on stable-11.0-ttsim, not master.
ARG TTSIM_QEMU_COMMIT=aa112ebe16f9c4ef68a838fa24fec04090018a28
ARG TT_KMD_TAG=ttkmd-2.11.0
# Keraunos support is in no tt-kmd release yet, so the Grendel row builds a second module from a
# branch. Every other card keeps the released driver above.
#
# This is the tip of the Keraunos TLB stack, not `keraunos` itself: handing TLB windows to userspace
# is tt-kmd #305 and #306, which `keraunos` does not carry, and without them every window the
# QuasarIoWindow suite asks for fails to allocate. Move this back to `keraunos` once the stack lands
# there.
ARG TT_KMD_KERAUNOS_REF=pjanevski/keraunos-tlb-attributes

# Stage 1: build QEMU with the ttsim PCI device into /opt/ttsim-qemu. ttsim-qemu is Tenstorrent's fork of
# QEMU that adds a `ttsim` PCI device, which forwards the guest's BAR accesses to libttsim and the
# simulator's DMA back into guest memory.
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

RUN git clone --filter=blob:none --no-checkout https://github.com/tenstorrent/ttsim-qemu.git /src \
    && cd /src && git checkout ${TTSIM_QEMU_COMMIT} \
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

# Stage 2b: tt-kmd with Keraunos support, kept as a separate module so the released driver stays
# what every other card loads.
FROM kmd AS kmd-keraunos

ARG TT_KMD_KERAUNOS_REF

RUN git clone --depth 1 --branch ${TT_KMD_KERAUNOS_REF} https://github.com/tenstorrent/tt-kmd.git /tt-kmd-keraunos \
    && make -C /tt-kmd-keraunos KDIR=/lib/modules/"$(cat /kver)"/build

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
COPY --from=kmd-keraunos /tt-kmd-keraunos/tenstorrent.ko /opt/tenstorrent-keraunos.ko
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
