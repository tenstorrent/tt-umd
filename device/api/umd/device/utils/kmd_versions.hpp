// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/utils/semver.hpp"

namespace tt::umd {

/**
 * KMD version 1.29.0 introduced IOMMU support. UMD requires at least this version to run with IOMMU enabled.
 * With never versions of KMD, UMD will still work when IOMMU is disabled on the system.
 */
inline constexpr SemVer KMD_IOMMU = SemVer(1, 29, 0);

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
 * KMD version 1.34.0 introduced support for configuring and using PCIe TLBs for buffer mappings. This feature enables
 * calls into KMD to reserve TLB by size, in order to enable multiple user processes to use the device safely at the
 * same time.
 */
inline constexpr SemVer KMD_TLBS = SemVer(1, 34, 0);
}  // namespace tt::umd
