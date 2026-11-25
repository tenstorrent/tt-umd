// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Server-directory management through SimulationConnector: it works in every build, with or without simulation
// support, and needs no simulator.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <vector>

#include "tests/test_utils/simulation_socket_test_utils.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"

using namespace tt::umd;
using tt::umd::test_utils::ScopedServerDirectory;

namespace {

// How many of these servers are the directory under test. The listing is machine-wide, so a test may only assert
// about its own directory: a concurrent run's servers are legitimately in there alongside it.
int count_directory(const std::vector<SimulationServerInfo>& servers, const std::filesystem::path& directory) {
    return static_cast<int>(
        std::count_if(servers.begin(), servers.end(), [&directory](const SimulationServerInfo& server) {
            return server.directory == directory;
        }));
}

}  // namespace

// A directory nothing serves from is a simulator build to host, not a server to attach to.
TEST(SimulationServerDirectory, DirectoryWithoutSocketsIsAHost) {
    const test_utils::SimulatorLayout simulator;

    EXPECT_EQ(SimulationConnector::role_for(simulator.directory()), SimulationConnector::Role::Host);
}

// A killed host leaves its sockets behind, and stat() cannot tell one of those from a live host's. Nothing can attach
// to that directory, so it is not a server: listing must both leave it out and clear it up, without anyone having to
// ask for a sweep.
TEST(SimulationServerDirectory, ListingLeavesOutAndClearsUpAServerWhoseHostIsGone) {
    // Nothing ever hosts this directory, so no server teardown removes it if an assertion bails.
    const ScopedServerDirectory directory(SimulationConnector::allocate_server_directory());
    test_utils::leave_stale_socket(directory.path() / "tt-umd-sim-0.sock");

    EXPECT_EQ(count_directory(SimulationConnector::list_servers(), directory.path()), 0);
    EXPECT_FALSE(std::filesystem::exists(directory.path()));
}

// The same sweep, reported rather than silent: a caller that wants to clear up without listing, and to say what it
// cleared up, gets the servers that were removed.
TEST(SimulationServerDirectory, PruneReportsTheServerItCleared) {
    const ScopedServerDirectory directory(SimulationConnector::allocate_server_directory());
    test_utils::leave_stale_socket(directory.path() / "tt-umd-sim-0.sock");

    EXPECT_EQ(count_directory(SimulationConnector::prune_dead_servers(), directory.path()), 1);
    EXPECT_FALSE(std::filesystem::exists(directory.path()));
    // Gone means gone: a second sweep has nothing left to report.
    EXPECT_EQ(count_directory(SimulationConnector::prune_dead_servers(), directory.path()), 0);
}

// Both halves come from one pass, so a caller acting on the live list and on what was swept sees one consistent view
// rather than two scans that can disagree.
TEST(SimulationServerDirectory, ScanReportsBothHalvesFromOnePass) {
    const ScopedServerDirectory gone(SimulationConnector::allocate_server_directory());
    const ScopedServerDirectory coming_up(SimulationConnector::allocate_server_directory());
    test_utils::leave_stale_socket(gone.path() / "tt-umd-sim-0.sock");

    const SimulationConnector::ServerScan scan = SimulationConnector::scan_servers();

    EXPECT_EQ(count_directory(scan.removed, gone.path()), 1);
    EXPECT_EQ(count_directory(scan.live, gone.path()), 0);
    // The socket-less directory is on the other side of the same pass.
    EXPECT_EQ(count_directory(scan.live, coming_up.path()), 1);
    EXPECT_FALSE(std::filesystem::exists(gone.path()));
    EXPECT_TRUE(std::filesystem::is_directory(coming_up.path()));
}

// A host that has claimed its directory and not bound its socket yet is indistinguishable from one that died before it
// could. Neither the listing nor the sweep may act on that, or a server would be swept out from under itself as it
// starts.
TEST(SimulationServerDirectory, KeepsAServerDirectoryWithNoSocketYet) {
    const ScopedServerDirectory directory(SimulationConnector::allocate_server_directory());

    EXPECT_EQ(count_directory(SimulationConnector::list_servers(), directory.path()), 1);
    EXPECT_EQ(count_directory(SimulationConnector::prune_dead_servers(), directory.path()), 0);
    EXPECT_TRUE(std::filesystem::is_directory(directory.path()));
}
