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

TEST(Assert, FormatMessage) {
    const std::map<std::pair<std::string, std::vector<std::string>>, std::string> test_cases = {
        {{"Hello {} and {}", {"world", "universe"}}, "Hello world and universe"},
        {{"The answer is {}", {"42"}}, "The answer is 42"},
        {{"No placeholders here", {"unused"}}, "No placeholders here"},
        {{"First {} and second {}", {"one"}}, "First one and second {}"},
        {{"Only {}", {"one", "two", "three"}}, "Only one"},
        {{"{}{}{}", {"A", "B", "C"}}, "ABC"}};

    for (const auto& [input, expected] : test_cases) {
        std::string result = tt::assert::format_message(input.first, input.second);
        EXPECT_EQ(result, expected) << "Input: '" << input.first << "'";
    }
}

TEST(Assert, ToStringSafe) {
    EXPECT_EQ(tt::assert::to_string_safe(42), "42");
    EXPECT_EQ(tt::assert::to_string_safe(3.14), "3.14");
    EXPECT_EQ(tt::assert::to_string_safe("hello"), "hello");
    EXPECT_EQ(tt::assert::to_string_safe(std::string("world")), "world");

    CustomType obj(123);
    EXPECT_EQ(tt::assert::to_string_safe(obj), "CustomType(123)");

    int a = 42;
    std::string b = "test";
    tt::OStreamJoin<int, std::string> join(a, b, " -> ");
    EXPECT_EQ(tt::assert::to_string_safe(join), "42 -> test");
}

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
        {"Custom type with formatting",
         [](std::stringstream& output) {
             CustomType obj(123);
             tt::assert::tt_assert_message(output, "Object: {}", obj);
         },
         "Object: CustomType(123)\n"},
        {"No formatting fallback",
         [](std::stringstream& output) { tt::assert::tt_assert_message(output, "First", "Second", "Third"); },
         "First\nSecond\nThird\n"},
        {"Mixed types",
         [](std::stringstream& output) {
             CustomType obj(456);
             tt::assert::tt_assert_message(output, "Mixed: {} and {}", obj, 3.14);
         },
         "Mixed: CustomType(456) and 3.14\n"},
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
         "Args: 1 2 3 4 5\n"}};

    for (const auto& test_case : test_cases) {
        std::stringstream output;
        test_case.test_func(output);
        EXPECT_EQ(output.str(), test_case.expected_output) << "Test: " << test_case.description;
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