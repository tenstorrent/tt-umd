FROM quay.io/pypa/manylinux_2_34_x86_64
# (AlmaLinux 9 based)
# Built wheels are also expected to be compatible with other distros using glibc 2.34 or later, including:
#     Debian 12+
#     Ubuntu 21.10+
#     Fedora 35+
#     CentOS/RHEL 9+

# Hack for CIv2 - fix mirror URLs
RUN FILES=(/etc/yum.repos.d/*.repo) && \
  sed --in-place -e 's/^mirrorlist=/# mirrorlist=/g' -e 's/^# baseurl=/baseurl=/' "${FILES[@]}" || true

# Install system dependencies matching docker_install_common.sh but using DNF
# This includes all the dependencies needed for building tt-umd
RUN dnf install -y \
    ninja-build \
    hwloc-devel \
    vim-common \
    && dnf clean all

# Set up environment variables for building
ENV CC=/opt/rh/gcc-toolset-14/root/bin/gcc
ENV CMAKE_C_COMPILER=/opt/rh/gcc-toolset-14/root/bin/gcc
ENV CXX=/opt/rh/gcc-toolset-14/root/bin/g++
ENV CMAKE_CXX_COMPILER=/opt/rh/gcc-toolset-14/root/bin/g++
