// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The address space picks the flat-address transport. mimir_local folds a flat window back to a
// chiplet AXI address. full_package leaves the flat address on the wire as the SPA. No server.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "grendel_flat_address.hpp"
#include "mimir.h"           // chippy
#include "mock_transport.h"  // chippy
#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/coordinates/grendel_noc_address_resolver.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"

namespace tt::umd::test {

TEST(GrendelFlatAddress, MimirLocalTranslatesToChipletAxi) {
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mimir_2x_package.yaml")));
    auto mimir0 = std::make_shared<chippy::transport::MockTransportInterface>();
    auto mimir1 = std::make_shared<chippy::transport::MockTransportInterface>();
    auto transport = make_grendel_flat_address_transport(soc_descriptor, {mimir0, mimir1}, "GrendelFlatAddress");

    // Mimir 1 CCE0. The flat slot is past Mimir 0; the chiplet AXI address is CCE0's local window.
    const GrendelAddressWindows windows = mimir_local_address_windows(soc_descriptor);
    const uint32_t locations_per_channel = static_cast<uint32_t>(soc_descriptor.get_grid_size(CoreType::DRAM).y);
    constexpr uint32_t kChannel = 1;
    constexpr uint32_t kLocation = 0;
    constexpr uint64_t kOffset = 0x800;
    const uint32_t l1_index = kChannel * locations_per_channel + kLocation;
    const uint64_t flat = windows.dram_l1_base + static_cast<uint64_t>(l1_index) * windows.dram_l1_stride + kOffset;
    const uint64_t local = chippy::grendel::kMimirCce0SramLocalAddr + kOffset;
    const uint32_t value = 0xA1B2C3D4;

    transport->write(flat, &value, sizeof(value));

    EXPECT_EQ(mimir1->mem[local], value);
    EXPECT_EQ(mimir1->mem.count(flat), 0u);
    EXPECT_EQ(mimir0->mem.count(local), 0u);
    EXPECT_EQ(mimir0->mem.count(flat), 0u);
}

TEST(GrendelFlatAddress, KeraunosSramAndControlUseTheK0Transport) {
    const SocDescriptor soc_descriptor(std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mmk.yaml")));
    auto mimir0 = std::make_shared<chippy::transport::MockTransportInterface>();
    auto mimir1 = std::make_shared<chippy::transport::MockTransportInterface>();
    auto keraunos = std::make_shared<chippy::transport::MockTransportInterface>();
    auto transport =
        make_grendel_flat_address_transport(soc_descriptor, {mimir0, mimir1, keraunos}, "GrendelFlatAddress");

    const GrendelNocAddressResolver resolver(soc_descriptor, mimir_local_address_windows(soc_descriptor));
    const CoreCoord eth = soc_descriptor.get_cores(CoreType::ETH).front();
    constexpr uint64_t kOffset = 0x800;
    const uint64_t sram = resolver.to_flat_address(eth, kOffset, NocId::NOC0);
    ASSERT_EQ(sram, 0x22800000ULL + kOffset);
    const uint32_t sram_value = 0xA1B2C3D4;
    transport->write(sram, &sram_value, sizeof(sram_value));
    EXPECT_EQ(keraunos->mem[sram], sram_value);
    EXPECT_EQ(mimir0->mem.count(sram), 0u);
    EXPECT_EQ(mimir1->mem.count(sram), 0u);

    const uint64_t control = resolver.keraunos_control_address(eth, grendel::KERAUNOS_CCE_PF_CTRL_RESET_OFFSET);
    ASSERT_EQ(control, 0x20200000ULL);
    const uint32_t control_value = 0x1;
    transport->write(control, &control_value, sizeof(control_value));
    EXPECT_EQ(keraunos->mem[control], control_value);
    EXPECT_EQ(mimir0->mem.count(control), 0u);
    EXPECT_EQ(mimir1->mem.count(control), 0u);

    // A Mimir CCE address still belongs to that Mimir's socket.
    const CoreCoord cce = soc_descriptor.get_dram_core_for_channel(1, 0, CoordSystem::NOC0);
    const uint64_t cce_sram =
        resolver.to_flat_address(cce, resolver.get_windows().dram_l1_noc_offset + kOffset, NocId::NOC0);
    const uint32_t cce_value = 0x11223344;
    transport->write(cce_sram, &cce_value, sizeof(cce_value));
    EXPECT_EQ(mimir1->mem[chippy::grendel::kMimirCce0SramLocalAddr + kOffset], cce_value);
    EXPECT_EQ(keraunos->mem.count(cce_sram), 0u);
    EXPECT_EQ(mimir0->mem.count(cce_sram), 0u);

    EXPECT_THROW(
        make_grendel_flat_address_transport(soc_descriptor, {mimir0, mimir1}, "GrendelFlatAddress"), std::exception);
}

TEST(GrendelFlatAddress, FullPackagePassesTheSpaThrough) {
    // Omitting address_space selects full_package. This Quasar descriptor does that.
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_simulation_1x1.yaml")));
    auto mock = std::make_shared<chippy::transport::MockTransportInterface>();
    auto transport = make_grendel_flat_address_transport(soc_descriptor, {mock}, "GrendelFlatAddress");

    constexpr uint64_t kSpa = 0x1280000800ULL;
    const uint32_t value = 0xC0FFEE11;
    transport->write(kSpa, &value, sizeof(value));

    uint32_t read_back = 0;
    transport->read(kSpa, &read_back, sizeof(read_back));
    EXPECT_EQ(read_back, value);
    EXPECT_EQ(mock->mem[kSpa], value);
}

TEST(GrendelFlatAddress, FullPackageUsesOneTransport) {
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_simulation_1x1.yaml")));
    auto first = std::make_shared<chippy::transport::MockTransportInterface>();
    auto second = std::make_shared<chippy::transport::MockTransportInterface>();
    EXPECT_THROW(
        make_grendel_flat_address_transport(soc_descriptor, {first, second}, "GrendelFlatAddress"), std::exception);
}

}  // namespace tt::umd::test
