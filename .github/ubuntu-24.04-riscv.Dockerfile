# ubuntu-24.04-riscv: EMULATED-NATIVE approach for building tt-umd for a riscv64 host.
#
# This is a real riscv64 image. On an x86 builder it runs under QEMU/binfmt, so
# from the toolchain's point of view the build is "native" (normal apt + cmake,
# no toolchain file), but every instruction is emulated, making builds slow.
FROM riscv64/ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
        ca-certificates \
        wget \
        git \
        git-lfs \
        build-essential \
        cmake \
        ninja-build \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
        libhwloc-dev \
    && rm -rf /var/lib/apt/lists/*
