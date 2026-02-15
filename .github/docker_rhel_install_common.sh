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

# @Development Tools installs unversioned /usr/bin/gcc only. Install gcc-13 / g++-13
# so CMake can use CC=gcc-13 CXX=g++-13 explicitly (Fedora 39 default is GCC 13).
echo "Installing versioned default GCC (gcc-13/g++-13)..."
$DNFC gcc-13 gcc-13-c++

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
