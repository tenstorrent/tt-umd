/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "remote_tt_sim_communicator.hpp"

#include <cstdint>
#include <vector>

#include "assert.hpp"
#include "message_data.hpp"
#include "process_manager.hpp"

namespace tt::umd {

RemoteTTSimCommunicator::RemoteTTSimCommunicator(ProcessManager* process_manager) :
    TTSimCommunicator(), process_manager_(process_manager) {
    TT_ASSERT(process_manager_ != nullptr, "RemoteTTSimCommunicator requires a non-null ProcessManager");
}

void RemoteTTSimCommunicator::pci_mem_write_bytes(uint64_t paddr, const void* data, uint32_t size) {
    PciMemWriteData header;
    header.paddr = paddr;
    header.size = size;
    process_manager_->send_message_with_data_and_response(
        MessageType::PCI_MEM_WRITE_BYTES, &header, sizeof(header), data, size);
}

void RemoteTTSimCommunicator::pci_mem_read_bytes(uint64_t paddr, void* data, uint32_t size) {
    PciMemReadData header;
    header.paddr = paddr;
    header.size = size;
    process_manager_->send_message_with_response(
        MessageType::PCI_MEM_READ_BYTES, &header, sizeof(header), data, size);
}

uint32_t RemoteTTSimCommunicator::pci_config_read32(uint32_t bus_device_function, uint32_t offset) {
    PciConfigRead32Data header;
    header.bus_device_function = bus_device_function;
    header.offset = offset;
    uint32_t value = 0;
    process_manager_->send_message_with_response(
        MessageType::PCI_CONFIG_READ32, &header, sizeof(header), &value, sizeof(value));
    return value;
}

}  // namespace tt::umd
