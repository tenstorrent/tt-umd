#!/bin/bash

# Define DNF command with the common flags
DNFC="dnf install -y --setopt=tsflags=nodocs"

echo "Updating system packages and installing prerequisites..."
dnf update -y

# Install tools group and core dependencies.
# '@Development Tools' provides the equivalent of 'build-essential' (gcc, g++, make).
# We install EPEL (Extra Packages for Enterprise Linux) to access more dev tools on RHEL/AlmaLinux.
$DNFC \
    epel-release \
    @Development\ Tools \
    git \
    git-lfs \
    wget \
    dnf-plugins-core \
    rpm-build \
    python3-pip \
    yamllint \
    xxd

# Add Kitware repository for latest CMake
echo "Adding Kitware repository for latest CMake..."
rpm --import https://apt.kitware.com/keys/kitware-archive-latest.asc || true
dnf config-manager --add-repo https://apt.kitware.com/kitware-yum-releases.repo || true
dnf makecache -y || true

# Install C++ development dependencies (using -devel suffix)
echo "Installing C++ development dependencies..."
$DNFC \
    cmake \
    ninja-build \
    libhwloc-devel \
    gtest-devel \
    yaml-cpp-devel \
    boost-devel \
    python3-devel 

# --- Handle Specific Compiler Versions (Clang 17) ---
echo "Installing Clang 17 and related tools (requires checking package names)..."

# On Fedora (and RHEL/Alma through EPEL/AppStream), clang packages are versioned.
# We will use the 'clang17' package family for Fedora (as confirmed by search) 
# and the official 'llvm-toolset' or specific packages for AlmaLinux 9/RHEL.
# Note: For Alma/RHEL 9, Clang 17 is often available in the AppStream repo via specific package names.

# Packages to install (check against AlmaLinux/RHEL 9 names first, then Fedora 39 names)
# AlmaLinux/RHEL often uses a simplified name for the current version:
CLANG_PACKAGES="clang llvm-libs clang-tools-extra"
# Fedora often uses versioned names (e.g., clang17)
FEDORA_CLANG_PACKAGES="clang17 clang17-libs clang17-tools-extra"
LIBCXX_PACKAGES="libcxx-devel" # Required for Clang's C++ standard library if not using GCC's

# Try installing Clang 17 packages
echo "Attempting to install versioned Clang 17..."

# 1. Try Fedora 39/EPEL names (e.g., clang17, which should include the others)
dnf install -y $FEDORA_CLANG_PACKAGES || true

# 2. Try RHEL/AlmaLinux 9/AppStream names (often just 'clang' for the latest supported version)
dnf install -y $CLANG_PACKAGES $LIBCXX_PACKAGES || true

# --- Link Clang Binaries ---
# RHEL/Fedora often use versioned binaries (e.g., clang-17), so we link them to unversioned names for simplicity
echo "Linking Clang binaries..."
if command -v clang-17 > /dev/null 2>&1; then
    ln -s /usr/bin/clang-17 /usr/bin/clang
    ln -s /usr/bin/clang++-17 /usr/bin/clang++
    ln -s /usr/bin/clang-format-17 /usr/bin/clang-format
else
    # Fallback to the default system clang if 17 isn't explicitly available
    echo "Warning: Clang 17 specific binaries not found. Relying on default system Clang."
fi


# --- Install Python dependencies ---
echo "Installing Python dependencies..."
python3 -m pip install --no-cache-dir pytest

echo "Cleanup DNF cache..."
dnf clean all