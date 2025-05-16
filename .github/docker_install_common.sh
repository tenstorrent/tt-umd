#!/bin/bash

UBUNTU_CODENAME=$(grep '^VERSION_CODENAME=' /etc/os-release | awk -F= '{print $2}' | tr -d '"')
export UBUNTU_CODENAME

# The below is to bring cmake from kitware
apt-get update
apt-get install -y --no-install-recommends ca-certificates gpg lsb-release wget software-properties-common gnupg jq
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $UBUNTU_CODENAME main" | tee /etc/apt/sources.list.d/kitware.list >/dev/null

# Install build and runtime deps
apt-get update
apt-get install -y \
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
    yamllint

# gcc-12 should be available only for ubuntu 22 and not 20
if apt-cache show gcc-12 > /dev/null 2>&1; then
    echo "gcc-12 is available. Installing..."
    apt-get install -y gcc-12 g++-12
else
    echo "gcc-12 is not available in the repository."
fi

# Install clang 17
wget https://apt.llvm.org/llvm.sh && \
    chmod u+x llvm.sh && \
    ./llvm.sh 17 && \
    apt install -y libc++-17-dev libc++abi-17-dev && \
    ln -s /usr/bin/clang-17 /usr/bin/clang && \
    ln -s /usr/bin/clang++-17 /usr/bin/clang++

# Install clang-format
apt install -y clang-format-17 && \
    ln -s /usr/bin/clang-format-17 /usr/bin/clang-format

apt-get clean && rm -rf /var/lib/apt/lists/*
