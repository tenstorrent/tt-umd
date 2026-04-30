/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include "eth_connection.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"

namespace tt::umd {

// TTSIM implementation backed by a TTSimCommunicator (handles dlopen / function-pointer machinery).
class TTSimChipImpl {
public:
    TTSimChipImpl(
        const std::filesystem::path& simulator_directory,
        ClusterDescriptor* cluster_desc,
        ChipId chip_id,
        bool duplicate_simulator_directory);
    ~TTSimChipImpl();

    void start_device();
    void close_device();

    void write_to_device(tt_xy_pair translated_core, const void* src, uint64_t l1_dest, uint32_t size);
    void read_from_device(tt_xy_pair translated_core, void* dest, uint64_t l1_src, uint32_t size);

    void clock(uint32_t clock);

    void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets);
    void assert_risc_reset(tt_xy_pair translated_core, const RiscType selected_riscs);
    void deassert_risc_reset(tt_xy_pair translated_core, const RiscType selected_riscs, bool staggered_start);
    bool connect_eth_links();

    TTSimCommunicator* get_communicator() { return communicator_.get(); }

private:
    void setup_ethernet_connections();

    ChipId chip_id_;
    ClusterDescriptor* cluster_desc_;
    std::unique_ptr<architecture_implementation> architecture_impl_;
    std::filesystem::path simulator_directory_;
    std::unordered_map<EthernetChannel, EthConnection> eth_connections_;

    std::unique_ptr<TTSimCommunicator> communicator_;
    uint32_t libttsim_pci_device_id = 0;
};

}  // namespace tt::umd
