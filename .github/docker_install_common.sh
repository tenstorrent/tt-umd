#!/bin/bash
set -euo pipefail

UBUNTU_VERSION=$(grep VERSION_ID /etc/os-release | cut -d'"' -f2)

# Install essential packages first (required for HTTPS and GPG operations)
apt-get update && apt-get install -y \
    ca-certificates \
    gnupg \
    wget

# Add Kitware repository for latest CMake
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $OS_CODENAME main" | tee /etc/apt/sources.list.d/kitware.list >/dev/null

# Install build and runtime deps
apt-get update && apt-get install -y \
    software-properties-common \
    build-essential \
    cmake \
    ninja-build \
    git \
    git-lfs \
    libhwloc-dev \
    libgtest-dev \
    libyaml-cpp-dev \
    libboost-all-dev \
    wget \
    yamllint \
    patchelf \
    xxd \
    rpm \
    dpkg-dev \
    fakeroot

# Install Python dependencies
apt-get update && apt-get install -y \
    python3-dev \
    python3-pip \
    python3-venv \
    python3-yaml \
    python3-pytest \
    python3-typing-extensions

# nanobind's stubgen.py requires typing_extensions on Python < 3.11 and needs TypeVarTuple (>= 4.1).
# Ubuntu 22.04's apt package is 3.10.0.2, which is too old, so install a newer one via pip.
# Ubuntu 24.04 uses Python 3.12 (stubgen doesn't need it, and pip is externally managed), so skip there.
if [ "${UBUNTU_VERSION}" = "22.04" ]; then
    python3 -m pip install --upgrade "typing_extensions>=4.6"
fi

# gcc-11 should be available only for ubuntu 22 and not 20
if apt-cache show gcc-11 > /dev/null 2>&1; then
    echo "gcc-11 is available. Installing..."
    apt-get install -y gcc-11 g++-11
else
    echo "gcc-11 is not available in the repository."
fi

# llvm.sh probes apt.llvm.org with wget --method=HEAD and treats a failed HEAD
# as "distro not supported". Pass -n so it uses OS_CODENAME (jammy/noble/focal)
# instead of the os-release VERSION string, and use --spider (GET) for the probe.
run_llvm_sh() {
    wget -q -O llvm.sh https://apt.llvm.org/llvm.sh
    chmod u+x llvm.sh
    sed -i 's/wget -q --method=HEAD/wget -q --spider/' llvm.sh
    ./llvm.sh "$1" -n "$OS_CODENAME"
}

# Install clang 13 only on Ubuntu 22.04 (obsolete on 24.04, so skip there).
if [ "${UBUNTU_VERSION}" = "22.04" ]; then
    echo "Installing clang-13 for minimum compiler version testing..."
    run_llvm_sh 13 && apt install -y libc++-13-dev libc++abi-13-dev
else
    echo "Skipping clang-13 (Ubuntu ${UBUNTU_VERSION}); not available or obsolete."
fi

# Install clang 20 as the default compiler.
run_llvm_sh 20 && \
    apt install -y libc++-20-dev libc++abi-20-dev && \
    ln -s /usr/bin/clang-20 /usr/bin/clang && \
    ln -s /usr/bin/clang++-20 /usr/bin/clang++

# Install clang-format
apt install -y clang-format-20 && \
    ln -s /usr/bin/clang-format-20 /usr/bin/clang-format

# Install clang-tidy-20
apt-get install -y clang-tidy-20
