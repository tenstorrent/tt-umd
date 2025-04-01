/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/remote_communication.h"

#include "logger.hpp"
#include "umd/device/driver_atomics.h"
#include "umd/device/lock_manager.h"
#include "umd/device/topology_utils.h"
#include "umd/device/umd_utils.h"

using namespace boost::interprocess;

struct remote_update_ptr_t {
    uint32_t ptr;
    uint32_t pad[3];
};

struct routing_cmd_t {
    uint64_t sys_addr;
    uint32_t data;
    uint32_t flags;
    uint16_t rack;
    uint16_t src_resp_buf_index;
    uint32_t local_buf_index;
    uint8_t src_resp_q_id;
    uint8_t host_mem_txn_id;
    uint16_t padding;
    uint32_t src_addr_tag;  // upper 32-bits of request source address.
};

namespace tt::umd {

RemoteCommunication::RemoteCommunication(TTDevice* tt_device) : tt_device(tt_device) {
    LockManager::initialize_mutex(MutexType::NON_MMIO, tt_device->get_pci_device()->get_device_num(), false);
}

RemoteCommunication::~RemoteCommunication() {
    LockManager::clear_mutex(MutexType::NON_MMIO, tt_device->get_pci_device()->get_device_num());
}

void RemoteCommunication::read_non_mmio(
    uint8_t* mem_ptr,
    tt_xy_pair core,
    uint64_t address,
    uint32_t size_in_bytes,
    eth_coord_t target_chip,
    const tt_xy_pair eth_core) {
    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);

    tt_xy_pair translated_core = core;
    core.x = translated_core.x;
    core.y = translated_core.y;

    auto host_address_params = tt_device->get_architecture_implementation()->get_host_address_params();
    auto eth_interface_params = tt_device->get_architecture_implementation()->get_eth_interface_params();
    auto noc_params = tt_device->get_architecture_implementation()->get_noc_params();

    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr;
    std::vector<std::uint32_t> erisc_q_ptrs =
        std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes * 2 / DATA_WORD_SIZE);
    std::vector<std::uint32_t> erisc_resp_q_wptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_resp_q_rptr = std::vector<uint32_t>(1);

    std::vector<std::uint32_t> data_block;

    routing_cmd_t* new_cmd;

    erisc_command.resize(sizeof(routing_cmd_t) / DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t*)&erisc_command[0];

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    auto lock = LockManager::get_mutex(MutexType::NON_MMIO, tt_device->get_pci_device()->get_device_num());

    const tt_xy_pair remote_transfer_ethernet_core = eth_core;

    tt_device->read_from_device(
        erisc_q_ptrs.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
        eth_interface_params.remote_update_ptr_size_bytes * 2);
    tt_device->read_from_device(
        erisc_resp_q_wptr.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
        DATA_WORD_SIZE);
    tt_device->read_from_device(
        erisc_resp_q_rptr.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes +
            eth_interface_params.remote_update_ptr_size_bytes,
        DATA_WORD_SIZE);

    bool full = is_non_mmio_cmd_q_full(eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];

    // bool use_dram;
    bool use_dram = false;
    uint32_t max_block_size;

    use_dram = size_in_bytes > 1024;
    max_block_size = use_dram ? host_address_params.eth_routing_block_size : eth_interface_params.max_block_size;

    uint32_t offset = 0;
    uint32_t block_size;
    uint32_t buffer_id = 0;

    while (offset < size_in_bytes) {
        while (full) {
            tt_device->read_from_device(
                erisc_q_rptr.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes +
                    eth_interface_params.remote_update_ptr_size_bytes,
                DATA_WORD_SIZE);
            full = is_non_mmio_cmd_q_full(eth_interface_params, erisc_q_ptrs[0], erisc_q_rptr[0]);
        }

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.cmd_buf_size_mask;
        if ((address + offset) & 0x1F) {  // address not 32-byte aligned
            block_size = DATA_WORD_SIZE;  // 4 byte aligned block
        } else {
            block_size = offset + max_block_size > size_in_bytes ? size_in_bytes - offset : max_block_size;
            // Align up to 4 bytes.
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;
        }
        uint32_t req_flags = block_size > DATA_WORD_SIZE
                                 ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_rd_req)
                                 : eth_interface_params.cmd_rd_req;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE
                                  ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_rd_data)
                                  : eth_interface_params.cmd_rd_data;
        uint32_t resp_rd_ptr = erisc_resp_q_rptr[0] & eth_interface_params.cmd_buf_size_mask;
        uint32_t host_dram_block_addr = host_address_params.eth_routing_buffers_start + resp_rd_ptr * max_block_size;
        uint16_t host_dram_channel = 0;  // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (use_dram && block_size > DATA_WORD_SIZE) {
            req_flags |= eth_interface_params.cmd_data_block_dram;
            resp_flags |= eth_interface_params.cmd_data_block_dram;
        }

        // Send the read request
        log_assert(
            (req_flags == eth_interface_params.cmd_rd_req) || (((address + offset) & 0x1F) == 0),
            "Block mode offset must be 32-byte aligned.");  // Block mode offset must be 32-byte aligned.
        new_cmd->sys_addr = get_sys_addr(noc_params, target_chip.x, target_chip.y, core.x, core.y, address + offset);
        new_cmd->rack = get_sys_rack(eth_interface_params, target_chip.rack, target_chip.shelf);
        new_cmd->data = block_size;
        new_cmd->flags = req_flags;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        tt_device->write_to_device(
            erisc_command.data(),
            remote_transfer_ethernet_core,
            eth_interface_params.request_routing_cmd_queue_base + (sizeof(routing_cmd_t) * req_wr_ptr),
            erisc_command.size() * DATA_WORD_SIZE);
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        tt_device->write_to_device(
            erisc_q_wptr.data(),
            remote_transfer_ethernet_core,
            eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
            erisc_q_wptr.size() * DATA_WORD_SIZE);
        tt_driver_atomics::sfence();
        // If there is more data to read and this command will make the q full, set full to 1.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full(eth_interface_params, (erisc_q_ptrs[0]), erisc_q_rptr[0])) {
            tt_device->read_from_device(
                erisc_q_ptrs.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                eth_interface_params.remote_update_ptr_size_bytes * 2);
            full = is_non_mmio_cmd_q_full(eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }

        // Wait for read request completion and extract the data into the `mem_ptr`

        // erisc firmware will:
        // 1. clear response flags
        // 2. start operation
        // 3. advance response wrptr
        // 4. complete operation and write data into response or buffer
        // 5. set response flags
        // So we have to wait for wrptr to advance, then wait for flags to be nonzero, then read data.

        do {
            tt_device->read_from_device(
                erisc_resp_q_wptr.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                DATA_WORD_SIZE);
        } while (erisc_resp_q_rptr[0] == erisc_resp_q_wptr[0]);
        tt_driver_atomics::lfence();
        uint32_t flags_offset = 12 + sizeof(routing_cmd_t) * resp_rd_ptr;
        std::vector<std::uint32_t> erisc_resp_flags = std::vector<uint32_t>(1);
        do {
            tt_device->read_from_device(
                erisc_resp_flags.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.response_routing_cmd_queue_base + flags_offset,
                DATA_WORD_SIZE);
        } while (erisc_resp_flags[0] == 0);

        if (erisc_resp_flags[0] == resp_flags) {
            tt_driver_atomics::lfence();
            uint32_t data_offset = 8 + sizeof(routing_cmd_t) * resp_rd_ptr;
            if (block_size == DATA_WORD_SIZE) {
                std::vector<std::uint32_t> erisc_resp_data = std::vector<uint32_t>(1);
                tt_device->read_from_device(
                    erisc_resp_data.data(),
                    remote_transfer_ethernet_core,
                    eth_interface_params.response_routing_cmd_queue_base + data_offset,
                    DATA_WORD_SIZE);
                if (size_in_bytes - offset < 4) {
                    // Handle misaligned (4 bytes) data at the end of the block.
                    // Only read remaining bytes into the host buffer, instead of reading the full uint32_t
                    std::memcpy((uint8_t*)mem_ptr + offset, erisc_resp_data.data(), size_in_bytes - offset);
                } else {
                    *((uint32_t*)mem_ptr + offset / DATA_WORD_SIZE) = erisc_resp_data[0];
                }
            } else {
                uint32_t buf_address = eth_interface_params.eth_routing_data_buffer_addr + resp_rd_ptr * max_block_size;
                size_buffer_to_capacity(data_block, block_size);
                tt_device->read_from_device(data_block.data(), remote_transfer_ethernet_core, buf_address, block_size);
                // assert(mem_ptr.size() - (offset/DATA_WORD_SIZE) >= (block_size * DATA_WORD_SIZE));
                log_assert(
                    (data_block.size() * DATA_WORD_SIZE) >= block_size,
                    "Incorrect data size read back from sysmem/device");
                // Account for misalignment by skipping any padding bytes in the copied data_block
                memcpy((uint8_t*)mem_ptr + offset, data_block.data(), std::min(block_size, size_in_bytes - offset));
            }
        }

        // Finally increment the rdptr for the response command q
        erisc_resp_q_rptr[0] = (erisc_resp_q_rptr[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        tt_device->write_to_device(
            erisc_resp_q_rptr.data(),
            remote_transfer_ethernet_core,
            eth_interface_params.response_cmd_queue_base + sizeof(remote_update_ptr_t) +
                eth_interface_params.cmd_counters_size_bytes,
            erisc_resp_q_rptr.size() * DATA_WORD_SIZE);
        tt_driver_atomics::sfence();
        log_assert(erisc_resp_flags[0] == resp_flags, "Unexpected ERISC Response Flags.");

        offset += block_size;
    }
}

void RemoteCommunication::write_to_non_mmio(
    uint8_t* mem_ptr,
    tt_xy_pair core,
    uint64_t address,
    uint32_t size_in_bytes,
    eth_coord_t target_chip,
    const tt_xy_pair eth_core) {
    static constexpr std::uint32_t NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 6;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 4;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_START_ID = 0;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_MASK = (NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS - 1);

    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);
    constexpr int BROADCAST_HEADER_SIZE = sizeof(data_word_t) * 8;  // Broadcast header is 8 words

    auto host_address_params = tt_device->get_architecture_implementation()->get_host_address_params();
    auto eth_interface_params = tt_device->get_architecture_implementation()->get_eth_interface_params();
    auto noc_params = tt_device->get_architecture_implementation()->get_noc_params();

    tt_xy_pair translated_core = core;
    core.x = translated_core.x;
    core.y = translated_core.y;

    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_q_ptrs =
        std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes * 2 / sizeof(uint32_t));

    std::vector<std::uint32_t> data_block;

    routing_cmd_t* new_cmd;

    uint32_t buffer_id = 0;
    // CMD_TIMESTAMP;
    uint32_t timestamp = 0;
    bool use_dram;
    uint32_t max_block_size;

    bool broadcast = false;

    // Broadcast requires block writes to host dram
    // use_dram = broadcast || (size_in_bytes > 256 * DATA_WORD_SIZE);
    use_dram = false;
    max_block_size = use_dram ? host_address_params.eth_routing_block_size : eth_interface_params.max_block_size;

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //

    auto lock = LockManager::get_mutex(MutexType::NON_MMIO, tt_device->get_pci_device()->get_device_num());

    bool non_mmio_transfer_cores_customized = false;
    int active_core_for_txn = 0;

    tt_xy_pair remote_transfer_ethernet_core = eth_core;

    erisc_command.resize(sizeof(routing_cmd_t) / DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t*)&erisc_command[0];
    tt_device->read_from_device(
        erisc_q_ptrs.data(),
        remote_transfer_ethernet_core,
        eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
        eth_interface_params.remote_update_ptr_size_bytes * 2);
    uint32_t full_count = 0;
    uint32_t offset = 0;
    uint32_t block_size;

    bool full = is_non_mmio_cmd_q_full(eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];
    while (offset < size_in_bytes) {
        while (full) {
            tt_device->read_from_device(
                erisc_q_rptr.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes +
                    eth_interface_params.remote_update_ptr_size_bytes,
                DATA_WORD_SIZE);
            full = is_non_mmio_cmd_q_full(eth_interface_params, erisc_q_ptrs[0], erisc_q_rptr[0]);
            full_count++;
        }
        // full = true;
        //  set full only if this command will make the q full.
        //  otherwise full stays false so that we do not poll the rd pointer in next iteration.
        //  As long as current command push does not fill up the queue completely, we do not want
        //  to poll rd pointer in every iteration.
        // full = is_non_mmio_cmd_q_full((erisc_q_ptrs[0] + 1) & CMD_BUF_PTR_MASK, erisc_q_rptr[0]);

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.cmd_buf_size_mask;
        if ((address + offset) & 0x1F) {  // address not 32-byte aligned
            block_size = DATA_WORD_SIZE;  // 4 byte aligned
        } else {
            // For broadcast we prepend a 32byte header. Decrease block size (size of payload) by this amount.
            block_size = offset + max_block_size > size_in_bytes + 32 * broadcast ? size_in_bytes - offset
                                                                                  : max_block_size - 32 * broadcast;
            // Explictly align block_size to 4 bytes, in case the input buffer is not uint32_t aligned
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;
        }
        // For 4 byte aligned data, transfer_size always == block_size. For unaligned data, transfer_size < block_size
        // in the last block
        uint64_t transfer_size =
            std::min(block_size, size_in_bytes - offset);  // Host side data size that needs to be copied
        // Use block mode for broadcast
        uint32_t req_flags = (broadcast || (block_size > DATA_WORD_SIZE))
                                 ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_wr_req | timestamp)
                                 : eth_interface_params.cmd_wr_req;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE
                                  ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_wr_ack)
                                  : eth_interface_params.cmd_wr_ack;
        timestamp = 0;

        uint32_t host_dram_block_addr =
            host_address_params.eth_routing_buffers_start +
            (active_core_for_txn * eth_interface_params.cmd_buf_size + req_wr_ptr) * max_block_size;
        uint16_t host_dram_channel = 0;  // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (req_flags & eth_interface_params.cmd_data_block) {
            // Copy data to sysmem or device DRAM for Block mode
            if (use_dram) {
                req_flags |= eth_interface_params.cmd_data_block_dram;
                resp_flags |= eth_interface_params.cmd_data_block_dram;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
            } else {
                uint32_t buf_address = eth_interface_params.eth_routing_data_buffer_addr + req_wr_ptr * max_block_size;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
                tt_device->write_to_device(
                    data_block.data(), remote_transfer_ethernet_core, buf_address, data_block.size() * DATA_WORD_SIZE);
            }
            tt_driver_atomics::sfence();
        }

        // Send the read request
        log_assert(
            broadcast || (req_flags == eth_interface_params.cmd_wr_req) || (((address + offset) % 32) == 0),
            "Block mode address must be 32-byte aligned.");  // Block mode address must be 32-byte aligned.

        new_cmd->sys_addr = get_sys_addr(noc_params, target_chip.x, target_chip.y, core.x, core.y, address + offset);
        new_cmd->rack = get_sys_rack(eth_interface_params, target_chip.rack, target_chip.shelf);

        if (req_flags & eth_interface_params.cmd_data_block) {
            // Block mode
            new_cmd->data = block_size + BROADCAST_HEADER_SIZE * broadcast;
        } else {
            if (size_in_bytes - offset < sizeof(uint32_t)) {
                // Handle misalignment at the end of the buffer:
                // Assemble a padded uint32_t from single bytes, in case we have less than 4 bytes remaining
                memcpy(&new_cmd->data, static_cast<const uint8_t*>(mem_ptr) + offset, size_in_bytes - offset);
            } else {
                new_cmd->data = *((uint32_t*)mem_ptr + offset / DATA_WORD_SIZE);
            }
        }

        new_cmd->flags = req_flags;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        tt_device->write_to_device(
            erisc_command.data(),
            remote_transfer_ethernet_core,
            eth_interface_params.request_routing_cmd_queue_base + (sizeof(routing_cmd_t) * req_wr_ptr),
            erisc_command.size() * DATA_WORD_SIZE);
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        tt_device->write_to_device(
            erisc_q_wptr.data(),
            remote_transfer_ethernet_core,
            eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
            erisc_q_wptr.size() * DATA_WORD_SIZE);
        tt_driver_atomics::sfence();

        offset += transfer_size;

        // If there is more data to send and this command will make the q full, switch to next Q.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full(
                eth_interface_params, (erisc_q_ptrs[0]) & eth_interface_params.cmd_buf_ptr_mask, erisc_q_rptr[0])) {
            active_core_for_txn++;
            // uint32_t update_mask_for_chip = remote_transfer_ethernet_cores[mmio_capable_chip_logical].size() - 1;
            uint32_t update_mask_for_chip = 1;
            active_core_for_txn =
                non_mmio_transfer_cores_customized
                    ? (active_core_for_txn & update_mask_for_chip)
                    : ((active_core_for_txn & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID);
            // active_core = (active_core & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID;
            // remote_transfer_ethernet_core =
            //     remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn];
            remote_transfer_ethernet_core = eth_core;
            tt_device->read_from_device(
                erisc_q_ptrs.data(),
                remote_transfer_ethernet_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                eth_interface_params.remote_update_ptr_size_bytes * 2);
            full = is_non_mmio_cmd_q_full(eth_interface_params, erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }
    }
}

void RemoteCommunication::wait_for_non_mmio_flush(std::vector<tt_xy_pair> remote_transfer_eth_cores) {
    auto eth_interface_params = tt_device->get_architecture_implementation()->get_eth_interface_params();

    std::vector<std::uint32_t> erisc_txn_counters = std::vector<uint32_t>(2);
    std::vector<std::uint32_t> erisc_q_ptrs =
        std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes * 2 / sizeof(uint32_t));

    // wait for all queues to be empty.
    std::vector<tt_xy_pair> active_eth_cores = remote_transfer_eth_cores;
    for (tt_xy_pair& eth_core : active_eth_cores) {
        do {
            tt_device->read_from_device(
                erisc_q_ptrs.data(),
                eth_core,
                eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes,
                eth_interface_params.remote_update_ptr_size_bytes * 2);
        } while (erisc_q_ptrs[0] != erisc_q_ptrs[4]);
    }
    // wait for all write responses to come back.
    for (tt_xy_pair& eth_core : active_eth_cores) {
        do {
            tt_device->read_from_device(
                erisc_txn_counters.data(), eth_core, eth_interface_params.request_cmd_queue_base, 8);
        } while (erisc_txn_counters[0] != erisc_txn_counters[1]);
    }
}

}  // namespace tt::umd
