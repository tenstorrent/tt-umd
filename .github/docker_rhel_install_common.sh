#!/bin/bash

# Define DNF command with the common flags
DNFC="dnf install -y --setopt=tsflags=nodocs"

echo "Updating system packages and installing prerequisites..."
dnf update -y

# Install tools group and core dependencies.
# '@Development Tools' provides the equivalent of 'build-essential' (gcc, g++, make).
$DNFC \
    @Development\ Tools \
    git \
    git-lfs \
    wget \
    dnf-plugins-core \
    rpm-build \
    python3-pip \
    yamllint \
    xxd \
    cpio \
    libnsl2 \
    libnsl2-devel

## Fedora ships a recent CMake; no external repo needed.

echo "Making gcc-13 and g++-13 symlinks, which are default versions of gcc in Fedora 39"
ln -s /usr/bin/gcc /usr/bin/gcc-13
ln -s /usr/bin/g++ /usr/bin/g++-13

# Install C++ development dependencies (using -devel suffix)
echo "Installing C++ development dependencies..."
$DNFC \
    cmake \
    ninja-build \
    hwloc-devel \
    gtest-devel \
    yaml-cpp-devel \
    boost-devel \
    python3-devel 

# --- Install Clang toolchain (Fedora) ---
echo "Installing Clang toolchain from Fedora repositories..."
$DNFC \
    clang \
    clang-tools-extra \
    llvm-libs \
    libcxx-devel


# --- Install Python dependencies ---
echo "Installing Python dependencies..."
python3 -m pip install --no-cache-dir pytest

echo "Cleanup DNF cache..."
dnf clean all
