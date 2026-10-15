// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "utils.hpp"

class RegisterAccess64BitOutsideQuasar : public ::testing::TestWithParam<tt::ARCH> {};

TEST_P(RegisterAccess64BitOutsideQuasar, Throws) {
    EXPECT_ANY_THROW(tt::umd::validate_register_access_width(GetParam(), tt::umd::RegisterAccessWidth::BITS64));
}

INSTANTIATE_TEST_SUITE_P(
    Architectures, RegisterAccess64BitOutsideQuasar, ::testing::Values(tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE));

TEST(RegisterAccess, Allows64BitAccessOnQuasar) {
    EXPECT_NO_THROW(tt::umd::validate_register_access_width(tt::ARCH::QUASAR, tt::umd::RegisterAccessWidth::BITS64));
}
