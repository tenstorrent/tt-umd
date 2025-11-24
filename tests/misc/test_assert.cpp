// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>

#include "assert.hpp"

struct CustomType {
    int value;

    CustomType(int v) : value(v) {}

    friend std::ostream& operator<<(std::ostream& os, const CustomType& obj) {
        return os << "CustomType(" << obj.value << ")";
    }
};

struct UnformattableType {
    int value;

    UnformattableType(int v) : value(v) {}

    friend std::ostream& operator<<(std::ostream& os, const UnformattableType& obj) {
        return os << "UnformattableType(" << obj.value << ")";
    }

    // Note: No fmt::formatter specialization - this makes it unformattable by fmt
};

TEST(Assert, AssertMessage) {
    struct TestCase {
        std::string description;
        std::function<void(std::stringstream&)> test_func;
        std::string expected_output;
    };

    std::vector<TestCase> test_cases = {
        {"Single argument",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "Single message"); },
         "Single message\n"},
        {"With formatting",
         [](std::stringstream& output) {
             int value = 42;
             tt::assert::tt_assert_message(output, "Value is {}", value);
         },
         "Value is 42\n"},
        {"Multiple args with formatting",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "Device: {}, Cores: {}", "TT123", 25); },
         "Device: TT123, Cores: 25\n"},
        {"No formatting fallback",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "First", "Second", "Third"); },
         "First\nSecond\nThird\n"},
        {"OStreamJoin",
         [](std::stringstream& output) {
             int a = 42;
             std::string b = "test";
             tt::OStreamJoin<int, std::string> join(a, b);
             tt::assert::tt_assert_message(output, "Join: {}", join);
         },
         "Join: 42 test\n"},
        {"Empty string", [](std::stringstream& output) { tt::assert::tt_assert_message(output, ""); }, "\n"},
        {"Only placeholders",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "{}", "replaced"); },
         "replaced\n"},
        {"Many arguments",
         [](std::stringstream& output) {
             tt::assert::tt_assert_message(output, "Args: {} {} {} {} {}", 1, 2, 3, 4, 5);
         },
         "Args: 1 2 3 4 5\n"},
        {"Boolean values",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "True: {}, False: {}", true, false); },
         "True: true, False: false\n"},
        {"Character values",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "Char: {}, Letter: {}", 'X', 'Y'); },
         "Char: X, Letter: Y\n"},
        {"Float and double",
         [](std::stringstream& output) {
             tt::assert::tt_assert_message(output, "Float: {}, Double: {}", 3.14f, 2.718);
         },
         "Float: 3.14, Double: 2.718\n"},
        {"String literals and objects",
         [](std::stringstream& output) {
             std::string str_obj = "object";
             tt::assert::tt_assert_message(output, "Literal: {}, Object: {}", "literal", str_obj);
         },
         "Literal: literal, Object: object\n"},
        {"Invalid format fallback",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "Invalid format {", "value"); },
         "Invalid format {\nvalue\n"},
        {"Mismatched braces fallback",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "Mismatched }", "value"); },
         "Mismatched }\nvalue\n"},
        {"Zero values",
         [](std::stringstream& output) {
             tt::assert::tt_assert_message(output, "Zero int: {}, Zero float: {}", 0, 0.0f);
         },
         "Zero int: 0, Zero float: 0\n"},
        {"Negative numbers",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "Negative: {} and {}", -42, -3.14); },
         "Negative: -42 and -3.14\n"},
        {"Long string",
         [](std::stringstream& output) {
             std::string long_str(100, 'A');
             tt::assert::tt_assert_message(output, "Long: {}", long_str);
         },
         "Long: " + std::string(100, 'A') + "\n"},
        {"Complex OStreamJoin",
         [](std::stringstream& output) {
             CustomType obj(789);
             int test_val = 100;
             tt::OStreamJoin<CustomType, int> join(obj, test_val, " -> ");
             tt::assert::tt_assert_message(output, "Complex join: {}", join);
         },
         "Complex join: CustomType(789) -> 100\n"}};

    for (const auto& test_case : test_cases) {
        std::stringstream output;
        test_case.test_func(output);
        // std::cout << output.str() << std::endl;
        EXPECT_EQ(output.str(), test_case.expected_output) << "Test: " << test_case.description;
    }
}

TEST(Assert, UnformattableTypes) {
    {
        std::stringstream output;
        EXPECT_THROW(
            {
                UnformattableType obj(456);
                tt::assert::tt_assert_message(output, "Unformattable: {}", obj);
            },
            std::runtime_error);
    }
}

TEST(Assert, MismatchedPlaceholders) {
    // Test cases where placeholder count doesn't match parameter count

    {
        std::stringstream output;
        EXPECT_THROW({ tt::assert::tt_assert_message(output, "Value {} and {} more", 42); }, std::runtime_error);
    }

    {
        std::stringstream output;
        EXPECT_THROW(
            { tt::assert::tt_assert_message(output, "Only {}", "first", "second", "third"); }, std::runtime_error);
    }
}

TEST(Assert, MacroIntegration) {
    try {
        TT_THROW("Error with value {}", 42);
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        std::string error_msg = e.what();
        EXPECT_TRUE(error_msg.find("Error with value 42") != std::string::npos);
    }

    try {
        TT_ASSERT(false, "Assertion failed with value {}", 123);
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        std::string error_msg = e.what();
        EXPECT_TRUE(error_msg.find("Assertion failed with value 123") != std::string::npos);
    }
}
