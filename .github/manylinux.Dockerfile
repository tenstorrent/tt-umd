FROM quay.io/pypa/manylinux_2_28_x86_64
# (AlmaLinux 8 based)
# Built wheels are also expected to be compatible with other distros using glibc 2.28 or later, including:
#     Debian 10+
#     Ubuntu 18.10+ (includes 20.04 LTS)
#     Fedora 29+
#     CentOS/RHEL 8+

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
ENV CXX=/opt/rh/gcc-toolset-14/root/bin/g++
