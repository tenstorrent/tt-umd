// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
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

const std::string cluster_id_example = "bh-glx-110-c01u02";

std::string read_cluster_desc(const std::string& cluster_desc_name) {
    std::ifstream fdesc(test_utils::GetClusterDescAbsPath(cluster_desc_name));
    EXPECT_FALSE(fdesc.fail());
    std::stringstream buffer;
    buffer << fdesc.rdbuf();
    return buffer.str();
}

// cluster_id is a top level key, so prepending it is enough. Single quoted so that values which are
// not legal cluster ids still reach the parser as strings.
std::string with_cluster_id(const std::string& cluster_desc_content, const std::string& cluster_id) {
    return "cluster_id: '" + cluster_id + "'\n" + cluster_desc_content;
}

// What resolve_cluster_id() is expected to return with no cluster id supplied: this machine's
// hostname, or nothing when that hostname cannot be expressed as a cluster id (long FQDN, exotic
// characters).
std::optional<std::string> local_os_hostname_as_cluster_id() {
    std::array<char, 256> hostname = {};
    if (gethostname(hostname.data(), hostname.size() - 1) != 0) {
        return std::nullopt;
    }
    std::string os_hostname(hostname.data());
    if (utils::get_cluster_id_error(os_hostname).has_value()) {
        return std::nullopt;
    }
    return os_hostname;
}

}  // namespace

TEST(ClusterDescriptorClusterIdTest, ClusterIdIsLoadedFromYaml) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml_content(
        with_cluster_id(read_cluster_desc("blackhole_P150.yaml"), cluster_id_example));

    ASSERT_TRUE(cluster_desc->get_cluster_id().has_value());
    EXPECT_EQ(cluster_desc->get_cluster_id().value(), cluster_id_example);
}

TEST(ClusterDescriptorClusterIdTest, ClusterIdIsUnsetWhenYamlOmitsIt) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml_content(read_cluster_desc("blackhole_P150.yaml"));

    EXPECT_FALSE(cluster_desc->get_cluster_id().has_value());
}

// Back-compat: no descriptor may be rejected because it predates this field. Deliberately does not
// assert the id is unset, so filling cluster_id into an example later is not a test failure.
TEST(ClusterDescriptorClusterIdTest, AllOfflineDescriptorsStillParse) {
    for (const std::string& cluster_desc_yaml : test_utils::GetAllClusterDescs()) {
        std::ifstream fdesc(cluster_desc_yaml);
        ASSERT_FALSE(fdesc.fail()) << cluster_desc_yaml;
        std::stringstream buffer;
        buffer << fdesc.rdbuf();

        EXPECT_NO_THROW(ClusterDescriptor::create_from_yaml_content(buffer.str())) << cluster_desc_yaml;
    }
}

TEST(ClusterDescriptorClusterIdTest, YamlAcceptsLegalClusterIds) {
    const std::string cluster_desc_content = read_cluster_desc("blackhole_P150.yaml");

    for (const std::string& cluster_id :
         {cluster_id_example,
          std::string("sjc1-tt-qb-01"),
          std::string("metal-wh-09"),
          std::string("bh-glx-110-c01u02.tenstorrent.com"),
          std::string("host_0"),
          std::string("a"),
          std::string(utils::CLUSTER_ID_MAX_LENGTH, 'a')}) {
        std::unique_ptr<ClusterDescriptor> cluster_desc;
        ASSERT_NO_THROW(
            cluster_desc =
                ClusterDescriptor::create_from_yaml_content(with_cluster_id(cluster_desc_content, cluster_id)))
            << cluster_id;
        EXPECT_EQ(cluster_desc->get_cluster_id().value(), cluster_id);
    }
}

// Parsing validates too, so every cluster id that reaches a descriptor is a legal one.
TEST(ClusterDescriptorClusterIdTest, YamlRejectsIllegalClusterIds) {
    const std::string cluster_desc_content = read_cluster_desc("blackhole_P150.yaml");

    // Empty, whitespace, path separators, and one over the limit.
    for (const std::string& cluster_id :
         {std::string(""),
          std::string("   "),
          std::string("has space"),
          std::string("foo/bar"),
          std::string("foo\\bar"),
          std::string(utils::CLUSTER_ID_MAX_LENGTH + 1, 'a')}) {
        EXPECT_THROW(
            ClusterDescriptor::create_from_yaml_content(with_cluster_id(cluster_desc_content, cluster_id)),
            error::UmdException<error::RuntimeError>)
            << "\"" << cluster_id << "\"";
    }
}

TEST(ClusterDescriptorClusterIdTest, SerializeOmitsClusterIdWhenUnset) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml_content(read_cluster_desc("blackhole_P150.yaml"));

    EXPECT_EQ(cluster_desc->serialize().find("cluster_id"), std::string::npos);
}

TEST(ClusterDescriptorClusterIdTest, SerializeRoundTripsClusterId) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml_content(
        with_cluster_id(read_cluster_desc("blackhole_P150.yaml"), cluster_id_example));

    const std::string serialized = cluster_desc->serialize();
    EXPECT_NE(serialized.find("cluster_id"), std::string::npos);

    std::unique_ptr<ClusterDescriptor> reparsed = ClusterDescriptor::create_from_yaml_content(serialized);
    ASSERT_TRUE(reparsed->get_cluster_id().has_value());
    EXPECT_EQ(reparsed->get_cluster_id().value(), cluster_id_example);
}

TEST(ClusterDescriptorClusterIdTest, ConstrainedDescriptorKeepsClusterId) {
    // Galaxy has many chips on one board, so constraining to a subset is not expanded back to the
    // full board and takes the chip id remapping path.
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml_content(
        with_cluster_id(read_cluster_desc("blackhole_galaxy.yaml"), cluster_id_example));
    ASSERT_GT(cluster_desc->get_number_of_chips(), 2);

    std::unique_ptr<ClusterDescriptor> constrained =
        ClusterDescriptor::create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1});

    ASSERT_LT(constrained->get_number_of_chips(), cluster_desc->get_number_of_chips());
    ASSERT_TRUE(constrained->get_cluster_id().has_value());
    EXPECT_EQ(constrained->get_cluster_id().value(), cluster_id_example);
}

TEST(ClusterDescriptorClusterIdTest, ConstrainedDescriptorKeepsClusterIdUnsetWhenSourceHasNone) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml_content(read_cluster_desc("blackhole_galaxy.yaml"));

    std::unique_ptr<ClusterDescriptor> constrained =
        ClusterDescriptor::create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1});

    EXPECT_FALSE(constrained->get_cluster_id().has_value());
}

TEST(ClusterDescriptorClusterIdTest, MockClusterHasNoClusterId) {
    std::unique_ptr<ClusterDescriptor> mock_cluster =
        ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::BLACKHOLE, true);

    EXPECT_FALSE(mock_cluster->get_cluster_id().has_value());
}

// What discovery stamps on the descriptor. Exercised through the helper rather than through
// TopologyDiscovery so that it runs without a card.
TEST(ResolveClusterIdTest, SuppliedClusterIdWins) {
    EXPECT_EQ(utils::resolve_cluster_id("tt-vm-host-7"), "tt-vm-host-7");
}

// Not a silent fallback to the OS hostname: that would produce a wrong-but-plausible topology,
// which is what supplying a cluster id exists to prevent.
TEST(ResolveClusterIdTest, IllegalSuppliedClusterIdThrows) {
    for (const std::string& cluster_id :
         {std::string(""),
          std::string("   "),
          std::string("has space"),
          std::string("has\ttab"),
          std::string("foo/bar"),
          std::string(utils::CLUSTER_ID_MAX_LENGTH + 1, 'a')}) {
        EXPECT_THROW(utils::resolve_cluster_id(cluster_id), error::UmdException<error::RuntimeError>)
            << "\"" << cluster_id << "\"";
    }
}

// With nothing supplied the OS hostname is used, and a hostname that cannot be expressed as a
// cluster id only warns and leaves the field unset -- it never throws, so discovery keeps working.
TEST(ResolveClusterIdTest, DefaultsToHostname) {
    EXPECT_EQ(utils::resolve_cluster_id(std::nullopt), local_os_hostname_as_cluster_id());
}
