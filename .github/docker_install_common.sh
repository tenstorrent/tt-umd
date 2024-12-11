#!/bin/bash

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
    yamllint

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

# Install newest GCC
apt install libmpfr-dev libgmp3-dev libmpc-dev -y && \
    wget https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.gz && \
    tar -xf gcc-14.2.0.tar.gz && \
    cd gcc-14.2.0 && \
    ./configure -v --build=x86_64-linux-gnu --host=x86_64-linux-gnu --target=x86_64-linux-gnu --prefix=/usr/local/gcc-14.2.0 --enable-checking=release --enable-languages=c,c++ --disable-multilib --program-suffix=-14.2.0 && \
    make -j && \
    make install && \
    ln -s /usr/local/gcc-14.2.0/bin/gcc-14.2.0 /usr/bin/gcc-14 && \
    ln -s /usr/local/gcc-14.2.0/bin/g++-14.2.0 /usr/bin/g++-14 && \
    rm gcc-14.2.0.tar.gz && \
    rm -rf gcc-14.2.0
