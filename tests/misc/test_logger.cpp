/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "common/logger_.hpp"
#include "common/timestamp.hpp"

using namespace tt::umd::util;

class LoggerTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir;
    std::filesystem::path log_file;

    void SetUp() override {
        // A bit of a hack - logger is only intended to be initialized once per
        // process, but we need to reset it for each test.
        tt::umd::logger::detail::is_initialized.store(false);

        std::string tmpl = (std::filesystem::temp_directory_path() / "logger_test_XXXXXX").string();
        int fd = mkstemp(tmpl.data());
        if (fd == -1) {
            throw std::runtime_error("Failed to create temporary file");
        }
        close(fd);
        log_file = tmpl;
        temp_dir = log_file.parent_path();
    }

    void TearDown() override {
        std::filesystem::remove_all(log_file);
        spdlog::shutdown();
    }

    // Helper to read entire file content
    std::string read_log_file() {
        std::ifstream file(log_file);
        std::stringstream buf;
        buf << file.rdbuf();
        return buf.str();
    }
};

TEST_F(LoggerTest, BasicLogging) {
    // Initialize logger with our test configuration
    tt::umd::logger::Options options;
    options.log_to_stderr = true;
    options.filename = log_file.string();
    options.pattern = "%v";  // Simple pattern for easier testing
    tt::umd::logger::initialize(options);

    // Write some test messages
    UMD_INFO("Test message 1");
    UMD_INFO("Test message 2");
    UMD_INFO("Test message 4");
    UMD_INFO("Test message 3");

    // Force flush by destroying the logger
    spdlog::drop_all();

    auto log_content = read_log_file();

    // Verify log content
    EXPECT_TRUE(log_content.find("Test message 1") != std::string::npos);
    EXPECT_TRUE(log_content.find("Test message 2") != std::string::npos);
    EXPECT_TRUE(log_content.find("Test message 3") != std::string::npos);
    EXPECT_TRUE(log_content.find("Test message 4") != std::string::npos);
}

TEST_F(LoggerTest, LogLevels) {
    tt::umd::logger::Options options;
    options.log_to_stderr = true;
    options.filename = log_file.string();
    options.pattern = "%v";
    options.log_level = spdlog::level::info;  // Set to INFO level
    tt::umd::logger::initialize(options);

    UMD_DEBUG("Debug message");  // Shouldn't appear
    UMD_INFO("Info message");    // Should appear
    UMD_ERROR("Error message");  // Should appear

    spdlog::drop_all();

    auto log_content = read_log_file();

    EXPECT_EQ(log_content.find("Debug message"), std::string::npos);
    EXPECT_TRUE(log_content.find("Info message") != std::string::npos);
    EXPECT_TRUE(log_content.find("Error message") != std::string::npos);
}

TEST_F(LoggerTest, FormatPatterns) {
    tt::umd::logger::Options options;
    options.log_to_stderr = false;
    options.filename = log_file.string();
    options.pattern = "[%l] %v";  // Level and message
    tt::umd::logger::initialize(options);

    UMD_INFO("Test message");

    spdlog::drop_all();

    auto log_content = read_log_file();

    EXPECT_TRUE(log_content.find("[info] Test message") != std::string::npos);
}

TEST_F(LoggerTest, MultipleInitialization) {
    tt::umd::logger::Options options;
    options.log_to_stderr = false;
    options.filename = log_file.string();
    options.pattern = "%v";

    // Initialize multiple times - should use first initialization only
    tt::umd::logger::initialize(options);

    UMD_INFO("First message");

    options.pattern = "DIFFERENT: %v";
    tt::umd::logger::initialize(options);  // Should be ignored

    UMD_INFO("Second message");

    spdlog::drop_all();

    auto log_content = read_log_file();

    EXPECT_TRUE(log_content.find("First message") != std::string::npos);
    EXPECT_TRUE(log_content.find("Second message") != std::string::npos);
    EXPECT_EQ(log_content.find("DIFFERENT:"), std::string::npos);
}

/**
 * The next few tests aren't really unit tests - just a mechanism to understand
 * the performance of the logger.  A log message that isn't printed (i.e. the
 * log level suppresses it) is a single-digit nanosecond penalty in a release
 * build on EPYC 7713 -- so pretty cheap.
 */
TEST_F(LoggerTest, DiskPerformance) {
    const size_t num_messages = 10;
    tt::umd::logger::Options options;
    options.log_to_stderr = false;
    options.filename = log_file.string();
    options.log_level = spdlog::level::info;
    tt::umd::logger::initialize(options);

    // Actually logged
    {
        Timestamp ts;
        for (size_t i = 0; i < num_messages; i++) {
            UMD_INFO("Test message");
        }
        std::cout << ts.to_string() << " for " << num_messages << " messages to disk" << std::endl;
    }

    // Not logged - should be faster
    {
        Timestamp ts;
        for (size_t i = 0; i < num_messages; i++) {
            UMD_TRACE("Shouldn't be logged");
        }
        std::cout << ts.to_string() << " for " << num_messages << " messages below level threshold" << std::endl;
    }
}

TEST_F(LoggerTest, StderrPerformance) {
    const size_t num_messages = 10;
    tt::umd::logger::Options options;
    options.log_to_stderr = true;
    options.filename = "";
    options.log_level = spdlog::level::info;
    tt::umd::logger::initialize(options);

    // Actually logged
    {
        Timestamp ts;
        for (size_t i = 0; i < num_messages; i++) {
            UMD_INFO("Test message");
        }
        std::cout << ts.to_string() << " for " << num_messages << " messages to stderr" << std::endl;
    }

    // Not logged - should be faster
    {
        Timestamp ts;
        for (size_t i = 0; i < num_messages; i++) {
            UMD_TRACE("Shouldn't be logged");
        }
        std::cout << ts.to_string() << " for " << num_messages << " messages below level threshold" << std::endl;
    }
}

TEST_F(LoggerTest, StderrAndDiskPerformance) {
    const size_t num_messages = 10;
    tt::umd::logger::Options options;
    options.log_to_stderr = true;
    options.filename = log_file.string();
    options.log_level = spdlog::level::info;
    tt::umd::logger::initialize(options);

    // Actually logged
    {
        Timestamp ts;
        for (size_t i = 0; i < num_messages; i++) {
            UMD_INFO("Test message");
        }
        std::cout << ts.to_string() << " for " << num_messages << " messages to disk & stderr" << std::endl;
    }

    // Not logged - should be faster
    {
        Timestamp ts;
        for (size_t i = 0; i < num_messages; i++) {
            UMD_TRACE("Shouldn't be logged");
        }
        std::cout << ts.to_string() << " for " << num_messages << " messages below level threshold" << std::endl;
    }
}
