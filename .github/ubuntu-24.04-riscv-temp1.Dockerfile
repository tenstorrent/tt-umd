# ubuntu-24.04-riscv-temp1: CROSS-COMPILE approach for building tt-umd for a riscv64 Linux host.
#
# This is a normal x86 image. It carries the GNU riscv64-linux-gnu cross toolchain
# plus a riscv64 copy of the only external system dependency UMD links (hwloc);
# every other dependency is fetched and built from source by CPM, so it
# cross-compiles along with UMD. Builds run at native x86 speed (no emulation).
# Pair it with cmake/riscv64-linux-gnu.cmake (passed as CMAKE_TOOLCHAIN_FILE).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Host-side build tooling (runs on x86) and the riscv64 GNU cross toolchain.
RUN apt-get update && apt-get install -y \
        ca-certificates \
        wget \
        git \
        git-lfs \
        cmake \
        ninja-build \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
        gcc-riscv64-linux-gnu \
        g++-riscv64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# Add riscv64 as a foreign architecture and pull the external system dependencies
# into the cross sysroot: hwloc (linked by UMD) and the riscv64 Python dev headers
# (nanobind needs the riscv64 pyconfig.h, which lives in the arch-specific multiarch
# include dir /usr/include/riscv64-linux-gnu/python3.*; the host x86 headers don't
# satisfy a riscv64 find_package(Python ... Development.Module)).
# riscv64 packages live on the Ubuntu ports mirror; amd64 stays on the default
# archive mirror, so each architecture is pinned to a mirror that actually serves it.
RUN dpkg --add-architecture riscv64 \
    && printf 'Types: deb\nURIs: http://archive.ubuntu.com/ubuntu/\nSuites: noble noble-updates noble-backports\nComponents: main restricted universe multiverse\nArchitectures: amd64\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n' > /etc/apt/sources.list.d/ubuntu.sources \
    && printf 'Types: deb\nURIs: http://ports.ubuntu.com/ubuntu-ports/\nSuites: noble noble-updates noble-backports\nComponents: main restricted universe multiverse\nArchitectures: riscv64\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n' > /etc/apt/sources.list.d/ubuntu-ports.sources \
    && apt-get update \
    && apt-get install -y libhwloc-dev:riscv64 libpython3-dev:riscv64 \
    && rm -rf /var/lib/apt/lists/*
