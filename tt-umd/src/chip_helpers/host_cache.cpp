// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <tt-umd/chip_helpers/host_cache.hpp>

// Detection for host cache maintenance: whether the ISA can perform it, and whether the platform
// needs it. Both run once per process. They live here rather than in the public header because they
// are policy rather than mechanism, and because they need <filesystem> and <fstream>, which no
// consumer of the header should have to parse.
//
// On architectures other than riscv64 this translation unit is empty: the header's constexpr stubs
// answer every query at compile time and no definition is needed.

#if defined(__riscv) && (__riscv_xlen == 64)

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>

#include <sys/syscall.h>
#if __has_include(<asm/hwprobe.h>)
#include <asm/hwprobe.h>
#define TT_UMD_HAVE_RISCV_HWPROBE 1
#endif

namespace tt::umd::host_cache {

namespace {

struct CboInfo {
    bool usable = false;
    uint32_t block_bytes = 0;
};

CboInfo probe_cbo() {
    CboInfo info;
#if defined(TT_UMD_HAVE_RISCV_HWPROBE) && defined(__NR_riscv_hwprobe) && \
    defined(RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE) && defined(RISCV_HWPROBE_EXT_ZICBOM)
    riscv_hwprobe pairs[2] = {
        {RISCV_HWPROBE_KEY_IMA_EXT_0, 0},
        {RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE, 0},
    };
    // The kernel only reports an extension here once it is usable from user mode, which is what the
    // call sites need -- Zicbom can be present in hardware but trapped at U-mode.
    if (::syscall(__NR_riscv_hwprobe, pairs, 2UL, 0UL, nullptr, 0U) != 0) {
        return info;
    }
    const auto block_bytes = static_cast<uint32_t>(pairs[1].value);
    if ((static_cast<uint64_t>(pairs[0].value) & RISCV_HWPROBE_EXT_ZICBOM) != 0 && block_bytes > 0 &&
        (block_bytes & (block_bytes - 1)) == 0) {
        info.usable = true;
        info.block_bytes = block_bytes;
    }
#endif
    return info;
}

// Walk up from a PCI device's sysfs node to the first ancestor that has an OF node, and report
// whether that node declares `dma-coherent`. A PCI endpoint has no OF node of its own unless the
// device tree describes it, so the walk is what lands on the PCIe controller -- the node whose
// coherency actually governs the card's inbound DMA.
bool of_ancestor_declares_coherent(const std::filesystem::path& start) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur = fs::canonical(start, ec);
    if (ec) {
        return false;
    }
    for (int depth = 0; depth < 8 && !cur.empty() && cur != cur.root_path(); ++depth) {
        const fs::path of_node = cur / "of_node";
        if (fs::exists(of_node, ec)) {
            return fs::exists(of_node / "dma-coherent", ec);
        }
        cur = cur.parent_path();
    }
    return false;
}

// True only when the platform is *known* to snoop this device's inbound DMA. Anything unknown is
// treated as non-coherent, so correctness does not depend on recognising every platform; the cost
// of being wrong that way is some redundant cache maintenance, not stale data.
//
// Checking for `dma-coherent` anywhere in the tree would be wrong: the SpacemiT K3 declares it on
// all eight of its internal hdma engines while leaving every pcie@* node without it, so a global
// search concludes "coherent" on exactly the board that is not.
bool platform_is_dma_coherent() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path pci_devices{"/sys/bus/pci/devices"};
    static constexpr std::string_view kTenstorrentVendorId = "0x1e52";
    for (const auto& dev : fs::directory_iterator(pci_devices, ec)) {
        if (ec) {
            break;
        }
        std::ifstream vendor(dev.path() / "vendor");
        std::string id;
        if (vendor && std::getline(vendor, id) && id == kTenstorrentVendorId) {
            return of_ancestor_declares_coherent(dev.path());
        }
    }
    return false;
}

// Everything the call sites need, from one evaluation.
//
// `enabled == false` means two very different things, and conflating them is how a non-coherent host
// ends up silently corrupting sysmem: either the platform snoops and no maintenance is needed, or it
// does not snoop and we cannot do anything about it. `required_but_unavailable` reports the second
// case so callers can say so loudly instead of proceeding as though all were well.
struct Decision {
    bool enabled = false;
    bool required_but_unavailable = false;
    uint32_t block_bytes = 0;
};

// Resolved on first use rather than at load time: the probe costs a syscall and a sysfs walk, and a
// process that links UMD without ever touching sysmem should not pay for them.
const Decision& decision() {
    static const Decision resolved = [] {
        const CboInfo cbo = probe_cbo();
        const bool coherent = platform_is_dma_coherent();
        return Decision{
            .enabled = cbo.usable && !coherent,
            .required_but_unavailable = !cbo.usable && !coherent,
            .block_bytes = cbo.block_bytes,
        };
    }();
    return resolved;
}

}  // namespace

bool available() { return decision().enabled; }

uint32_t block_bytes() { return decision().block_bytes; }

bool maintenance_required_but_unavailable() { return decision().required_but_unavailable; }

}  // namespace tt::umd::host_cache

#endif
