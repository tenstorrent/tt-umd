// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <memory>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/tt_device/protocol/scalar_noc_access.hpp"
#include "umd/device/tt_device_model/quasar_tt_device_model.hpp"
#include "umd/device/types/arch.hpp"

using namespace tt::umd;

namespace {

// A model that performs no access, so the components can be inspected without a driver handle.
class InertScalarNocAccess : public ScalarNocAccess {
public:
    void read(uint64_t, uint64_t*, uint32_t, uint32_t) override {}

    void write(uint64_t, uint64_t, uint32_t, uint32_t) override {}
};

std::unique_ptr<QuasarTTDeviceModel> make_model() {
    return std::make_unique<QuasarTTDeviceModel>(std::make_unique<InertScalarNocAccess>(), 0, nullptr);
}

}  // namespace

// TTDevice requires these four; a model that returns nullptr for any of them cannot be driven.
TEST(QuasarTTDeviceModelTest, SuppliesTheComponentsTTDeviceRequires) {
    auto model = make_model();

    EXPECT_NE(model->get_device_protocol(), nullptr);
    EXPECT_NE(model->get_device_firmware(), nullptr);
    EXPECT_NE(model->get_architecture_impl(), nullptr);
    EXPECT_NE(model->get_soc_arch_descriptor(), nullptr);
    EXPECT_NE(model->get_shared_soc_arch_descriptor(), nullptr);
}

// Quasar has none of these on this path: no mapped BAR window, no DMA engine the driver exposes,
// no JTAG link and no remote hop. Saying so with nullptr is what lets TTDevice take the other
// route instead of calling into something that would fail.
TEST(QuasarTTDeviceModelTest, DeclaresTheInterfacesItDoesNotHave) {
    auto model = make_model();

    EXPECT_EQ(model->get_pcie_interface(), nullptr);
    EXPECT_EQ(model->get_dma_interface(), nullptr);
    EXPECT_EQ(model->get_jtag_interface(), nullptr);
    EXPECT_EQ(model->get_remote_interface(), nullptr);
}

TEST(QuasarTTDeviceModelTest, IsAQuasarModel) {
    auto model = make_model();

    EXPECT_EQ(model->get_shared_soc_arch_descriptor()->get_arch(), tt::ARCH::QUASAR);
}
