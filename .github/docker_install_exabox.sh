#!/bin/bash

# Provisioning for the exabox multihost CI image, layered on top of
# docker_install_common.sh. It lives in its own script so the shared CI images
# do not carry an sshd, an ipmitool and a passwordless-sudo account that only
# the galaxy multihost runners need.

set -euo pipefail

apt-get update

# mpirun launches the job by SSHing between the per-host containers, so every
# worker image has to be able to accept those connections.
# Keep config tweaks after the apt install so openssh-server does not overwrite them.
apt-get install -y --no-install-recommends openssh-server sudo

# ipmitool for UMD warm_reset (Wormhole UBB raw IPMI) and host BMC access.
# The container still needs /dev/ipmi* from the host; this only installs the CLI.
apt-get install -y --no-install-recommends ipmitool

# ttop-ipmi-reset, provided by the runner's sidecar, drives the reset over a unix
# socket with socat. Installing it here keeps the workflow off live apt repos.
apt-get install -y --no-install-recommends socat

# mpirun and the reset tooling run as this account on every host.
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
