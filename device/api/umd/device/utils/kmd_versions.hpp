// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/utils/semver.hpp"

namespace tt::umd {

/**
 * The minimum KMD version that can be used to interact with Tenstorrent devices in UMD.
 */
inline constexpr SemVer KMD_MINIMUM_VERSION = SemVer(2, 0, 0);

/**
 * KMD version 2.0.0 introduced support for mapping buffers to NOC by using IOCTL. Before 2.0.0, UMD used to access
 * iATU configuration registers directly to perform such mappings. KMD exposed this functionality via IOCTL which brings
 * the ability to map buffers from multiple processes safely. While it's still possible to use direct register access
 * for mapping buffers to NOC on KMD versions older than 2.0.0, it's discouraged to do so.
 */
inline constexpr SemVer KMD_MAP_TO_NOC = SemVer(2, 0, 0);

/**
 * KMD version 2.4.1 introduced architecture agnostic reset support. With the new IOCTL in KMD 2.4.1, by using the same
 * IOCTL UMD can now reset different architectures without needing to have architecture specific reset IOCTLs.
 */
inline constexpr SemVer KMD_ARCH_AGNOSTIC_RESET = SemVer{2, 4, 1};

/**
 * KMD version 2.6.0 introduced the TENSTORRENT_IOCTL_SET_POWER_STATE IOCTL for explicit per-client power domain
 * management. Opening the device with O_APPEND opts out of legacy mode, allowing idle devices to reduce power even
 * while application connections remain active.
 */
inline constexpr SemVer KMD_POWER_STATE = SemVer(2, 6, 0);

/**
 * KMD version 2.9.0 introduced read-only page pinning. The IOMMU mapping permits device reads while faulting device
 * writes, and allows device-readable mappings of read-only and shared file-backed memory.
 */
inline constexpr SemVer KMD_READ_ONLY_PAGE_PINNING = SemVer(2, 9, 0);

/**
 * KMD version 2.10.0 introduced the TENSTORRENT_IOCTL_EXPORT_TLB_DMABUF IOCTL, which exports an
 * allocated and configured TLB window as a Linux dma-buf fd for peer-to-peer PCIe DMA (e.g. RDMA
 * NIC import via ibv_reg_dmabuf_mr()).
 */
inline constexpr SemVer KMD_TLB_DMABUF_EXPORT = SemVer(2, 10, 0, 1);

/**
 * TENSTORRENT_IOCTL_EXPORT_TLB_DMABUF also requires the running kernel itself to be new enough
 * (Linux 5.8+), independent of the KMD version, since the ioctl relies on dma-buf facilities
 * introduced in that release.
 */
inline constexpr SemVer MIN_KERNEL_TLB_DMABUF_EXPORT = SemVer(5, 8, 0);
}  // namespace tt::umd
