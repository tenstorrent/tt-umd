#include "gtest/gtest.h"
#include "comparison.hpp"

#include <cstdint>
#include <vector>

namespace tt::umd::test::utils {


TEST(ComparisonTest, PayloadWriteAlignedRunLengths) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 32;
    static_assert(vec_size % run_length == 0, "vec_size must be a multiple of run_length");
    std::vector<uint32_t> data(vec_size);

    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = run_length
    };
    payload_spec.write_payload(data);

    std::vector<uint32_t> expected_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < expected_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto end = expected_data.data() + std::min<std::size_t>(i + payload_spec.run_length, expected_data.size());
        std::fill(expected_data.data() + i, end, value);
    }

    ASSERT_EQ(data, expected_data);
}

TEST(ComparisonTest, PayloadWriteUnalignedRunLengths) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 15;
    static_assert(vec_size % run_length != 0, "vec_size must not be a multiple of run_length");
    std::vector<uint32_t> data(vec_size);

    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = run_length
    };
    payload_spec.write_payload(data);

    std::vector<uint32_t> expected_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < expected_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto end = expected_data.data() + std::min<std::size_t>(i + payload_spec.run_length, expected_data.size());
        std::fill(expected_data.data() + i, end, value);
    }

    ASSERT_EQ(data, expected_data);
}

TEST(ComparisonTest, PayloadWriteRunLengthLargerThanVector) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 9999;
    static_assert(vec_size % run_length != 0, "vec_size must not be a multiple of run_length");
    std::vector<uint32_t> data(vec_size);

    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = vec_size + 1
    };
    payload_spec.write_payload(data);

    std::vector<uint32_t> expected_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < expected_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto run_length = std::min<std::size_t>(payload_spec.run_length, expected_data.size() - i);
        std::fill_n(expected_data.data() + i, run_length, value);
    }

    ASSERT_EQ(data, expected_data);
}

TEST(ComparisonTest, PayloadReadReportsMismatchWithMisalignedRunLengths) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 15;
    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = run_length
    };

    std::vector<uint32_t> compared_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < compared_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto run_length = std::min<std::size_t>(payload_spec.run_length, compared_data.size() - i);
        std::fill_n(compared_data.data() + i, run_length, value);
    }
    compared_data.at(78) = 0;

    ASSERT_FALSE(payload_matches(compared_data, payload_spec));
}

TEST(ComparisonTest, PayloadReadReportsMismatchWithRunLengthLargerThanVector) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 9999;
    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = run_length
    };

    std::vector<uint32_t> compared_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < compared_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto run_length = std::min<std::size_t>(payload_spec.run_length, compared_data.size() - i);
        std::fill_n(compared_data.data() + i, run_length, value);
    }
    compared_data.at(78) = 0;

    ASSERT_FALSE(payload_matches(compared_data, payload_spec));
}

TEST(ComparisonTest, PayloadReadReportsMatchWithMisalignedRunLengths) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 15;
    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = run_length
    };

    std::vector<uint32_t> compared_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < compared_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto run_length = std::min<std::size_t>(payload_spec.run_length, compared_data.size() - i);
        std::fill_n(compared_data.data() + i, run_length, value);
    }

    ASSERT_TRUE(payload_matches(compared_data, payload_spec));
}

TEST(ComparisonTest, PayloadReadReportsMismatchWithAlignedRunLengths) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 32;
    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = run_length
    };

    std::vector<uint32_t> compared_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < compared_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto run_length = std::min<std::size_t>(payload_spec.run_length, compared_data.size() - i);
        std::fill_n(compared_data.data() + i, run_length, value);
    }
    compared_data.at(78) = 0;

    ASSERT_FALSE(payload_matches(compared_data, payload_spec));
}

TEST(ComparisonTest, PayloadReadReportsMatchWithAlignedRunLengths) {
    static constexpr std::size_t vec_size = 1024;
    static constexpr std::size_t run_length = 32;
    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = run_length
    };

    std::vector<uint32_t> compared_data(vec_size);
    uint32_t value = payload_spec.start;

    for (std::size_t i = 0; i < compared_data.size(); i += payload_spec.run_length, value += payload_spec.increment) {
        auto end = compared_data.data() + std::min<std::size_t>(i + payload_spec.run_length, compared_data.size());
        std::fill(compared_data.data() + i, end, value);
    }

    ASSERT_TRUE(payload_matches(compared_data, payload_spec));
}


TEST(ComparisonTest, PayloadWriteReadMatchesWithMisalignedRunLengths) {
    std::vector<uint32_t> data(1024);

    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = 23
    };

    payload_spec.write_payload(data);
    ASSERT_TRUE(payload_matches(data, payload_spec));
}

TEST(ComparisonTest, PayloadWriteReadMatchesWithAlignedRunLengths) {
    std::vector<uint32_t> data(1024);

    payload_spec_t<uint32_t> payload_spec = {
        .start = 1,
        .increment = 1,
        .run_length = 64
    };

    payload_spec.write_payload(data);
    ASSERT_TRUE(payload_matches(data, payload_spec));
}

}; // namespace tt::umd::test::utils