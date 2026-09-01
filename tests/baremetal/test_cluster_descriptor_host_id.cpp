// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

#include "common/utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/utils/error.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

const std::string host_id_example = "bh-glx-110-c01u02";

std::string read_cluster_desc(const std::string& cluster_desc_name) {
    std::ifstream fdesc(test_utils::GetClusterDescAbsPath(cluster_desc_name));
    EXPECT_FALSE(fdesc.fail());
    std::stringstream buffer;
    buffer << fdesc.rdbuf();
    return buffer.str();
}

// host_id is a top level key, so prepending it is enough.
std::string with_host_id(const std::string& cluster_desc_content, const std::string& host_id) {
    return "host_id: " + host_id + "\n" + cluster_desc_content;
}

// What local_host_id() is expected to return with TT_HOST_ID unset: this machine's hostname, or
// nothing when that hostname cannot be expressed as a host id (long FQDN, exotic characters).
std::optional<std::string> local_os_hostname_as_host_id() {
    std::array<char, 256> hostname = {};
    if (gethostname(hostname.data(), hostname.size() - 1) != 0) {
        return std::nullopt;
    }
    const std::string os_hostname(hostname.data());
    if (utils::get_host_id_error(os_hostname).has_value()) {
        return std::nullopt;
    }
    return os_hostname;
}

// Sets TT_HOST_ID for one test and always clears it afterwards, so a failing expectation cannot
// leak the variable into the tests that follow.
class ClusterDescriptorHostIdEnvTest : public ::testing::Test {
protected:
    void set_env_host_id(const std::string& value) {
        ASSERT_EQ(setenv(utils::TT_HOST_ID_ENV.data(), value.c_str(), 1), 0);
    }

    void TearDown() override { ASSERT_EQ(unsetenv(utils::TT_HOST_ID_ENV.data()), 0); }
};

}  // namespace

TEST(ClusterDescriptorHostIdTest, HostIdIsLoadedFromYaml) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml_content(
        with_host_id(read_cluster_desc("blackhole_P150.yaml"), host_id_example));

    ASSERT_TRUE(cluster_desc->get_host_id().has_value());
    EXPECT_EQ(cluster_desc->get_host_id().value(), host_id_example);
}

TEST(ClusterDescriptorHostIdTest, HostIdIsUnsetWhenYamlOmitsIt) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml_content(read_cluster_desc("blackhole_P150.yaml"));

    EXPECT_FALSE(cluster_desc->get_host_id().has_value());
}

// Back-compat: no descriptor may be rejected because it predates this field. Deliberately does not
// assert the id is unset, so filling host_id into an example later is not a test failure.
TEST(ClusterDescriptorHostIdTest, AllOfflineDescriptorsStillParse) {
    for (const std::string& cluster_desc_yaml : test_utils::GetAllClusterDescs()) {
        std::ifstream fdesc(cluster_desc_yaml);
        ASSERT_FALSE(fdesc.fail()) << cluster_desc_yaml;
        std::stringstream buffer;
        buffer << fdesc.rdbuf();

        EXPECT_NO_THROW(ClusterDescriptor::create_from_yaml_content(buffer.str())) << cluster_desc_yaml;
    }
}

TEST(ClusterDescriptorHostIdTest, SerializeOmitsHostIdWhenUnset) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml_content(read_cluster_desc("blackhole_P150.yaml"));

    EXPECT_EQ(cluster_desc->serialize().find("host_id"), std::string::npos);
}

TEST(ClusterDescriptorHostIdTest, SerializeRoundTripsHostId) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml_content(
        with_host_id(read_cluster_desc("blackhole_P150.yaml"), host_id_example));

    const std::string serialized = cluster_desc->serialize();
    EXPECT_NE(serialized.find("host_id"), std::string::npos);

    std::unique_ptr<ClusterDescriptor> reparsed = ClusterDescriptor::create_from_yaml_content(serialized);
    ASSERT_TRUE(reparsed->get_host_id().has_value());
    EXPECT_EQ(reparsed->get_host_id().value(), host_id_example);
}

TEST(ClusterDescriptorHostIdTest, SetHostIdAcceptsLegalValues) {
    ClusterDescriptor cluster_desc;

    for (const std::string& host_id :
         {host_id_example,
          std::string("sjc1-tt-qb-01"),
          std::string("metal-wh-09"),
          std::string("bh-glx-110-c01u02.tenstorrent.com"),
          std::string("host_0"),
          std::string("a"),
          std::string(utils::HOST_ID_MAX_LENGTH, 'a')}) {
        EXPECT_NO_THROW(cluster_desc.set_host_id(host_id)) << host_id;
        EXPECT_EQ(cluster_desc.get_host_id().value(), host_id);
    }
}

TEST(ClusterDescriptorHostIdTest, SetHostIdRejectsIllegalValues) {
    ClusterDescriptor cluster_desc;

    // Empty, whitespace, path separators, a leading/trailing separator, and one over the limit.
    for (const std::string& host_id :
         {std::string(""),
          std::string("   "),
          std::string("has space"),
          std::string("has\ttab"),
          std::string("foo/bar"),
          std::string("foo\\bar"),
          std::string(".leading-dot"),
          std::string("trailing-dot."),
          std::string("-leading-dash"),
          std::string(utils::HOST_ID_MAX_LENGTH + 1, 'a')}) {
        EXPECT_THROW(cluster_desc.set_host_id(host_id), error::RuntimeError) << "\"" << host_id << "\"";
    }

    // A rejected value must not have been stored.
    EXPECT_FALSE(cluster_desc.get_host_id().has_value());
}

TEST(ClusterDescriptorHostIdTest, ConstrainedDescriptorKeepsHostId) {
    // Galaxy has many chips on one board, so constraining to a subset is not expanded back to the
    // full board and takes the chip id remapping path.
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml_content(
        with_host_id(read_cluster_desc("blackhole_galaxy.yaml"), host_id_example));
    ASSERT_GT(cluster_desc->get_number_of_chips(), 2);

    std::unique_ptr<ClusterDescriptor> constrained =
        ClusterDescriptor::create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1});

    ASSERT_LT(constrained->get_number_of_chips(), cluster_desc->get_number_of_chips());
    ASSERT_TRUE(constrained->get_host_id().has_value());
    EXPECT_EQ(constrained->get_host_id().value(), host_id_example);
}

TEST(ClusterDescriptorHostIdTest, ConstrainedDescriptorKeepsHostIdUnsetWhenSourceHasNone) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml_content(read_cluster_desc("blackhole_galaxy.yaml"));

    std::unique_ptr<ClusterDescriptor> constrained =
        ClusterDescriptor::create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1});

    EXPECT_FALSE(constrained->get_host_id().has_value());
}

TEST_F(ClusterDescriptorHostIdEnvTest, LocalHostIdPrefersEnvVar) {
    set_env_host_id("tt-vm-host-7");

    const std::optional<std::string> host_id = utils::local_host_id();
    ASSERT_TRUE(host_id.has_value());
    EXPECT_EQ(host_id.value(), "tt-vm-host-7");
}

TEST_F(ClusterDescriptorHostIdEnvTest, LocalHostIdTrimsEnvVar) {
    set_env_host_id("  tt-vm-host-7\n");

    const std::optional<std::string> host_id = utils::local_host_id();
    ASSERT_TRUE(host_id.has_value());
    EXPECT_EQ(host_id.value(), "tt-vm-host-7");
}

// An exported but empty variable is a launcher accident, not a request for an empty host id.
TEST_F(ClusterDescriptorHostIdEnvTest, LocalHostIdTreatsEmptyEnvVarAsUnset) {
    const std::optional<std::string> expected = local_os_hostname_as_host_id();

    for (const std::string& value : {std::string(""), std::string("   ")}) {
        set_env_host_id(value);
        EXPECT_EQ(utils::local_host_id(), expected) << "\"" << value << "\"";
    }
}

// Not a silent fallback to the OS hostname: that would produce a wrong-but-plausible topology,
// which is what TT_HOST_ID exists to prevent.
TEST_F(ClusterDescriptorHostIdEnvTest, LocalHostIdThrowsOnInvalidEnvVar) {
    for (const std::string& value :
         {std::string("has space"),
          std::string("foo/bar"),
          std::string("trailing-dot."),
          std::string(utils::HOST_ID_MAX_LENGTH + 1, 'a')}) {
        set_env_host_id(value);
        EXPECT_THROW(utils::local_host_id(), error::RuntimeError) << "\"" << value << "\"";
    }
}

// With no env var the OS hostname is used, and a hostname that cannot be expressed as a host id
// only warns and leaves the field unset -- it never throws, so discovery keeps working.
TEST_F(ClusterDescriptorHostIdEnvTest, LocalHostIdFallsBackToHostname) {
    ASSERT_EQ(unsetenv(utils::TT_HOST_ID_ENV.data()), 0);

    EXPECT_EQ(utils::local_host_id(), local_os_hostname_as_host_id());
}

// The env var describes the machine this process runs on. A mock descriptor describes some other
// machine, so parsing must never pick the variable up.
TEST_F(ClusterDescriptorHostIdEnvTest, EnvVarDoesNotLeakIntoYamlParsing) {
    set_env_host_id("tt-vm-host-7");

    const std::string cluster_desc_content = read_cluster_desc("blackhole_P150.yaml");

    std::unique_ptr<ClusterDescriptor> without_key = ClusterDescriptor::create_from_yaml_content(cluster_desc_content);
    EXPECT_FALSE(without_key->get_host_id().has_value());

    std::unique_ptr<ClusterDescriptor> with_key =
        ClusterDescriptor::create_from_yaml_content(with_host_id(cluster_desc_content, host_id_example));
    ASSERT_TRUE(with_key->get_host_id().has_value());
    EXPECT_EQ(with_key->get_host_id().value(), host_id_example);
}

TEST_F(ClusterDescriptorHostIdEnvTest, MockClusterHasNoHostId) {
    set_env_host_id("tt-vm-host-7");

    std::unique_ptr<ClusterDescriptor> mock_cluster =
        ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::BLACKHOLE, true);

    EXPECT_FALSE(mock_cluster->get_host_id().has_value());
}
