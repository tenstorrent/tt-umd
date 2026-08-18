#!/bin/bash

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
    python3-dev \
    python3-pip \
    python3-venv \
    patchelf \
    xxd \
    rpm \
    dpkg-dev \
    fakeroot

# Install Python dependencies
python3 -m pip install --no-cache-dir pytest pyyaml

# gcc-11 should be available only for ubuntu 22 and not 20
if apt-cache show gcc-11 > /dev/null 2>&1; then
    echo "gcc-11 is available. Installing..."
    apt-get install -y gcc-11 g++-11
else
    echo "gcc-11 is not available in the repository."
fi

# Install clang 13 only on Ubuntu 22.04 (obsolete on 24.04, so skip there).
UBUNTU_VERSION=$(grep VERSION_ID /etc/os-release | cut -d'"' -f2)
if [ "${UBUNTU_VERSION}" = "22.04" ]; then
    echo "Installing clang-13 for minimum compiler version testing..."
    wget https://apt.llvm.org/llvm.sh && \
        chmod u+x llvm.sh && \
        ./llvm.sh 13 && \
        apt install -y libc++-13-dev libc++abi-13-dev
else
    echo "Skipping clang-13 (Ubuntu ${UBUNTU_VERSION}); not available or obsolete."
fi

# Install clang 20 as the default compiler.
wget https://apt.llvm.org/llvm.sh && \
    chmod u+x llvm.sh && \
    ./llvm.sh 20 && \
    apt install -y libc++-20-dev libc++abi-20-dev && \
    ln -s /usr/bin/clang-20 /usr/bin/clang && \
    ln -s /usr/bin/clang++-20 /usr/bin/clang++

# Install clang-format
apt install -y clang-format-20 && \
    ln -s /usr/bin/clang-format-20 /usr/bin/clang-format

# Install clang-tidy-20
apt-get install -y clang-tidy-20

# OpenSSH server for dedicated exabox multihost workers (mpirun SSHes into this image).
# Keep config tweaks after the apt install so openssh-server does not overwrite them.
apt-get install -y --no-install-recommends openssh-server sudo
if ! id -u user >/dev/null 2>&1; then
    adduser --uid 1001 --shell /bin/bash --disabled-password --gecos "" user
fi
usermod -aG sudo user
echo 'user ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/user
chmod 0440 /etc/sudoers.d/user
mkdir -p /run/sshd
grep -q '^StrictModes no' /etc/ssh/sshd_config || echo "StrictModes no" >> /etc/ssh/sshd_config

# OpenMPI with ULFM — must match the exabox runner's launch agent path
# (/opt/openmpi-v5.0.7-ulfm/bin/prted). Same package metal installs for multihost.
# SHA256 pinned for supply-chain integrity of the GitHub release artifact.
OMPI_PREFIX="/opt/openmpi-v5.0.7-ulfm"
OMPI_DEB_URL="https://github.com/tenstorrent/ompi/releases/download/v5.0.7/openmpi-ulfm_5.0.7-1_amd64.deb"
OMPI_DEB_SHA256="954e872d9105e8bf8c31368ff7a5db8670a3d549e2e7eb1ab6072cffcae7984d"
OMPI_DEB_FILE="$(basename "$OMPI_DEB_URL")"
OMPI_TMP="$(mktemp -d)"
wget -q -O "${OMPI_TMP}/${OMPI_DEB_FILE}" "${OMPI_DEB_URL}"
echo "${OMPI_DEB_SHA256} ${OMPI_TMP}/${OMPI_DEB_FILE}" | sha256sum -c -
apt-get install -y --no-install-recommends "${OMPI_TMP}/${OMPI_DEB_FILE}"
rm -rf "${OMPI_TMP}"
test -x "${OMPI_PREFIX}/bin/prted"
test -x "${OMPI_PREFIX}/bin/mpirun"

# Login shells (profile.d) and non-login/PAM sessions (/etc/environment) both need
# mpirun on PATH; GHA container steps often skip a login shell.
echo "export PATH=${OMPI_PREFIX}/bin:\$PATH" > /etc/profile.d/openmpi.sh
echo "export LD_LIBRARY_PATH=${OMPI_PREFIX}/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}" >> /etc/profile.d/openmpi.sh
chmod 0644 /etc/profile.d/openmpi.sh
if ! grep -q "${OMPI_PREFIX}/bin" /etc/environment 2>/dev/null; then
    if grep -q '^PATH="' /etc/environment 2>/dev/null; then
        sed -i "s|^PATH=\"|PATH=\"${OMPI_PREFIX}/bin:|" /etc/environment
    elif grep -q '^PATH=' /etc/environment 2>/dev/null; then
        sed -i "s|^PATH=|PATH=${OMPI_PREFIX}/bin:|" /etc/environment
    else
        echo "PATH=\"${OMPI_PREFIX}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\"" >> /etc/environment
    fi
fi
