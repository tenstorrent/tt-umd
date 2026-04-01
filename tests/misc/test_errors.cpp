// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <stdexcept>

#include "umd/device/utils/error_detail.hpp"

using namespace tt::umd::error;

struct TestErrorData {
    int cookie;
};

struct TestError : UmdError<TestErrorData> {
    explicit TestError(int cookie = 238) :
        UmdError<TestErrorData>(fmt::format("This is an UMD error. Cookie: {}", cookie), {.cookie = cookie}) {}
};

TEST(UmdException, Macros) {
    EXPECT_THROW(UMD_THROW(TestError, 200), UmdException<TestError>);
    EXPECT_THROW(UMD_ASSERT(false, TestError), UmdException<TestError>);
    EXPECT_NO_THROW(UMD_ASSERT(true, TestError));
    EXPECT_THROW(UMD_THROW_OR_RETURN(true, TestError, 200), UmdException<TestError>);
    EXPECT_NO_THROW(UMD_THROW_OR_RETURN(false, TestError, 200));
    auto error = UMD_THROW_OR_RETURN(false, TestError, 200);
    EXPECT_EQ(error.data().cookie, 200);
}

TEST(UmdException, ExceptionData) {
    uint32_t expected_line = 0;
    try {
        // clang-format off
        expected_line = __LINE__; UMD_THROW(TestError, 1234);
        // clang-format on
    } catch (UmdException<TestError> &test_error) {
        EXPECT_NO_THROW(test_error.error().data());
        EXPECT_EQ(test_error.error().data().cookie, 1234);

        EXPECT_EQ(test_error.file(), __FILE__);
        EXPECT_GT(test_error.backtrace().size(), 0);
        EXPECT_EQ(test_error.line(), expected_line);
        std::cout << test_error.what() << std::endl;
    } catch (...) {
        FAIL();
    }
}

TEST(UmdException, DeepCatch) {
    bool caught_umd_error = false;
    std::string umd_error_what;
    try {
        try {
            UMD_THROW(TestError);
        } catch (UmdException<TestError> &error) {
            caught_umd_error = true;
            umd_error_what = error.what();
            throw;
        }
    } catch (std::runtime_error &runtime_error) {
        std::cout << runtime_error.what() << std::endl;
        EXPECT_EQ(umd_error_what, runtime_error.what());
    } catch (...) {
        FAIL();
    }
    ASSERT_TRUE(caught_umd_error);
}

TEST(UmdException, AssertCondition) {
    try {
        UMD_ASSERT(1 == 2, TestError);
        FAIL();
    } catch (UmdException<TestError> &error) {
        EXPECT_EQ("1 == 2", error.condition());
        std::cout << error.what() << std::endl;
    }
}
