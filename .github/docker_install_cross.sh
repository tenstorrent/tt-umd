#!/bin/bash
set -euo pipefail

# Install cross-compilation toolchains for aarch64 and riscv64.
# This script must run after docker_install_common.sh so that the Kitware
# and LLVM apt repos are already present and can be pinned to amd64.

# Pin Kitware cmake repo to amd64 only.
# The cmake package is not available for other architectures, so without this
# pinning, apt-get update would produce errors after adding cross-arch to dpkg.
if [ -f /etc/apt/sources.list.d/kitware.list ]; then
    sed -i 's|^deb \[signed-by=|deb [arch=amd64 signed-by=|g' \
        /etc/apt/sources.list.d/kitware.list
fi

# Pin LLVM repo to amd64 only (clang is not available for other architectures).
# The repo filename installed by llvm.sh varies, so grep for the LLVM domain.
grep -rl 'apt\.llvm\.org' /etc/apt/sources.list.d/ 2>/dev/null | grep '\.list$' \
    | xargs -r sed -i \
        -e 's|^deb \[|deb [arch=amd64 |g' \
        -e 's|^deb http://apt\.llvm\.org|deb [arch=amd64] http://apt.llvm.org|g'

# Pin main Ubuntu sources to amd64 only so that apt-get update does not try
# to fetch arm64/riscv64 package lists from archive.ubuntu.com (which only
# serves amd64/i386 and returns 404 for other architectures).
sed -i \
    -e 's|^deb http://archive.ubuntu.com|deb [arch=amd64] http://archive.ubuntu.com|g' \
    -e 's|^deb http://security.ubuntu.com|deb [arch=amd64] http://security.ubuntu.com|g' \
    /etc/apt/sources.list

# Add ports.ubuntu.com as the package source for arm64 and riscv64.
# ports.ubuntu.com is the canonical mirror for Ubuntu non-x86 architectures.
UBUNTU_CODENAME=$(. /etc/os-release && echo "$VERSION_CODENAME")
cat >> /etc/apt/sources.list <<EOF
deb [arch=arm64,riscv64] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME} main restricted universe multiverse
deb [arch=arm64,riscv64] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME}-updates main restricted universe multiverse
deb [arch=arm64,riscv64] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME}-backports main restricted universe multiverse
deb [arch=arm64,riscv64] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME}-security main restricted universe multiverse
EOF

# Enable cross-compilation target architectures.
dpkg --add-architecture arm64
dpkg --add-architecture riscv64
apt-get update

# Cross-compilation toolchains.
apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    gcc-riscv64-linux-gnu \
    g++-riscv64-linux-gnu

# Cross-arch hwloc dev packages needed to link tt-umd for the target arch.
apt-get install -y \
    libhwloc-dev:arm64 \
    libhwloc-dev:riscv64
