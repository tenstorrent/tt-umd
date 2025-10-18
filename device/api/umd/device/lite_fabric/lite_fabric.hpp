// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/ranges.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/cluster.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/lite_fabric/fabric_edm_types.hpp"
#include "umd/device/lite_fabric/lf_dev_mem_map.hpp"
#include "umd/device/lite_fabric/lite_fabric_constants.hpp"
#include "umd/device/lite_fabric/lite_fabric_header.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

namespace lite_fabric {

#define is_power_of_2(x) (((x) > 0) && (((x) & ((x)-1)) == 0))

template <size_t LIMIT = 0, typename T>
auto wrap_increment(T val) -> T {
    constexpr bool is_pow2 = LIMIT != 0 && is_power_of_2(LIMIT);
    if constexpr (LIMIT == 1) {
        return val;
    } else if constexpr (LIMIT == 2) {
        return 1 - val;
    } else if constexpr (is_pow2) {
        return (val + 1) & (static_cast<T>(LIMIT - 1));
    } else {
        return (val == static_cast<T>(LIMIT - 1)) ? static_cast<T>(0) : static_cast<T>(val + 1);
    }
}

/*
Initialization process for Lite Fabric

    1. Host writes the lite fabric kernel to an arbitrary active ethernet core on MMIO capable chips. This
    is designated as the Primary core with an initial state of ETH_INIT_LOCAL. This core will launch
    lite fabric kernels on other active ethernet cores on the same chip with an initial state of
ETH_INIT_LOCAL_HANDSHAKE.

    2. The primary core will stall for the ETH_INIT_LOCAL_HANDSHAKE cores to be ready

    3. Primary core transitions state to ETH_INIT_NEIGHBOUR. It will launch a primary lite fabric kernel on the eth
device.

    4. Subordinate core transitions state to ETH_INIT_NEIGHBOUR_HANDSHAKE

    5. The primary lite fabric kernel on the eth device will launch lite fabric kernels on other active ethernet cores
on the eth device with an initial state of ETH_INIT_LOCAL_HANDSHAKE
*/

enum class InitState : uint16_t {
    // Unknown initial state.
    UNKNOWN = 0,
    // Indicates that this is written directly from host.
    ETH_INIT_FROM_HOST,
    // Write kernel to local ethernet cores and wait for ack.
    ETH_INIT_LOCAL,
    // Wait for ack from connected ethernet core.
    ETH_HANDSHAKE_NEIGHBOUR,
    // Write primary kernel to connected ethernet core and wait for ack.
    ETH_INIT_NEIGHBOUR,
    // Wait for ack from local ethernet cores.
    ETH_HANDSHAKE_LOCAL,
    // Ready for traffic.
    READY,
    // Terminated.
    TERMINATED,
};

struct LiteFabricConfig {
    // Starting address of the Lite Fabric binary to be copied locally and to the neighbour.
    volatile uint32_t binary_addr = 0;

    // Size of the Lite Fabric binary.
    volatile uint32_t binary_size = 0;

    // Bit N is 1 if channel N is an active ethernet core. Relies on eth_chan_to_noc_xy to
    // get the ethernet core coordinate.
    volatile uint32_t eth_chans_mask = 0;

    unsigned char padding0[4];

    // Subordinate cores on the same chip increment this value when they are ready. The primary core
    // will stall until this value shows all eth cores are ready.
    volatile uint32_t primary_local_handshake = 0;

    unsigned char padding1[12];

    // Becomes 1 when the neighbour is ready.
    volatile uint32_t neighbour_handshake = 0;

    unsigned char padding2[14];

    volatile uint16_t is_primary = false;

    volatile uint8_t primary_eth_core_x = 0;

    volatile uint8_t primary_eth_core_y = 0;

    volatile uint16_t is_mmio = false;

    volatile InitState initial_state = InitState::UNKNOWN;

    volatile InitState current_state = InitState::UNKNOWN;

    // Set to 1 to enable routing.
    volatile uint32_t routing_enabled = 1;
} __attribute__((packed));

static_assert(sizeof(LiteFabricConfig) % 16 == 0);
static_assert(offsetof(LiteFabricConfig, primary_local_handshake) % 16 == 0);
static_assert(offsetof(LiteFabricConfig, neighbour_handshake) % 16 == 0);

class HostToLiteFabricReadEvent {
private:
    inline static std::atomic<uint64_t> event{0};

public:
    static uint64_t get() { return event.load(); }

    static void increment() { event.fetch_add(1); }
};

// Interface for Host to MMIO Lite Fabric
template <uint32_t NUM_BUFFERS, uint32_t CHANNEL_BUFFER_SIZE>
struct HostToLiteFabricInterface {
    // This values are updated by the device and read to the host.
    struct DeviceToHost {
        volatile uint8_t fabric_sender_channel_index = 0;
        volatile uint8_t fabric_receiver_channel_index = 0;
    } __attribute((packed)) d2h;

    // These values are updated by the host and written to the device.
    struct HostToDevice {
        volatile uint8_t sender_host_write_index = 0;
        volatile uint8_t receiver_host_read_index = 0;
    } __attribute((packed)) h2d;

    uint32_t host_interface_on_device_addr = 0;
    uint32_t sender_channel_base = 0;
    uint32_t receiver_channel_base = 0;
    uint32_t eth_barrier_addr = 0;
    uint32_t tensix_barrier_addr = 0;
    uint32_t l1_alignment_bytes = 0;
    // The core to process requests.
    uint32_t mmio_device_id = 0;
    uint32_t mmio_eth_core_x = 0;
    uint32_t mmio_eth_core_y = 0;
    TTDevice* tt_device = nullptr;

    inline void init() volatile {
        h2d.sender_host_write_index = 0;
        h2d.receiver_host_read_index = 0;
        d2h.fabric_sender_channel_index = 0;
        d2h.fabric_receiver_channel_index = 0;
    }

    void read(void* mem_ptr, size_t size, CoreCoord receiver_core, tt_xy_pair src_core, uint64_t src_addr) {
        uint64_t src_noc_addr = (uint64_t(src_core.y) << (36 + 6)) | (uint64_t(src_core.x) << 36) | src_addr;
        read_noc_addr(mem_ptr, size, receiver_core, src_noc_addr);
    }

    void write(void* mem_ptr, size_t size, CoreCoord sender_core, tt_xy_pair dst_core, uint64_t dst_addr) {
        uint64_t dst_noc_addr = (uint64_t(dst_core.y) << (36 + 6)) | (uint64_t(dst_core.x) << 36) | dst_addr;
        write_noc_addr(mem_ptr, size, sender_core, dst_noc_addr);
    }

    void barrier(CoreCoord translated_core_sender) {
        uint32_t barrier_value = 0xca11ba11;
        const auto do_barrier =
            [&](const CoreCoord& translated_core, const std::string& core_type_name, uint32_t barrier_addr) -> void {
            const uint64_t dest_noc_upper =
                (uint64_t(translated_core.y) << (36 + 6)) | (uint64_t(translated_core.x) << 36);
            uint64_t dest_noc_addr = dest_noc_upper | (uint64_t)barrier_addr;
            write_one_page(&barrier_value, sizeof(uint32_t), translated_core_sender, dest_noc_addr);

            uint32_t read_barrier = 0;
            read_one_page(&read_barrier, sizeof(uint32_t), translated_core_sender, dest_noc_addr);

            if (read_barrier != barrier_value) {
                throw std::runtime_error(fmt::format(
                    "Lite fabric barrier failed. Chip memory corruption on {} translated core ({}, {}): barrier value "
                    "mismatch {:#x} != {:#x}",
                    core_type_name,
                    translated_core.x,
                    translated_core.y,
                    read_barrier,
                    barrier_value));
            }
        };

        CoreCoord barrier_coord = CoreCoord(1, 2, CoreType::TENSIX, CoordSystem::TRANSLATED);
        do_barrier(barrier_coord, "tensix", tensix_barrier_addr);
        barrier_coord = CoreCoord(1, 1, CoreType::ETH, CoordSystem::NOC0);
        do_barrier(barrier_coord, "ethernet", eth_barrier_addr);
    }

private:
    constexpr uint32_t get_max_payload_data_size_bytes() const {
        // Additional 64B to be used only for unaligned reads/writes.
        return CHANNEL_BUFFER_SIZE - sizeof(FabricLiteHeader) - GLOBAL_ALIGNMENT;
    }

    uint32_t get_next_send_buffer_slot_address(uint32_t channel_address) const {
        auto buffer_index = h2d.sender_host_write_index;
        return channel_address + buffer_index * CHANNEL_BUFFER_SIZE;
    }

    uint32_t get_next_receiver_buffer_slot_address(uint32_t channel_address) const {
        auto buffer_index = h2d.receiver_host_read_index;
        return channel_address + buffer_index * CHANNEL_BUFFER_SIZE;
    }

    void wait_for_empty_write_slot(CoreCoord translated_core_sender) {
        uint32_t offset = offsetof(HostToLiteFabricInterface, d2h);
        do {
            tt_device->read_from_device(
                (void*)(reinterpret_cast<uintptr_t>(this) + offset),
                translated_core_sender,
                host_interface_on_device_addr + offset,
                sizeof(DeviceToHost));
        } while ((h2d.sender_host_write_index + 1) % NUM_BUFFERS == d2h.fabric_sender_channel_index);
    }

    void wait_for_read_event(CoreCoord translated_core_sender, uint32_t read_event_addr) {
        tt_driver_atomics::mfence();
        volatile FabricLiteHeader header;
        header.command_fields.noc_read.event = 0;
        const auto expectedOrderId = HostToLiteFabricReadEvent::get();
        while (true) {
            tt_device->read_from_device(
                const_cast<void*>(static_cast<volatile void*>(&header)),
                translated_core_sender,

                read_event_addr,
                sizeof(FabricLiteHeader));
            if (header.command_fields.noc_read.event == expectedOrderId) {
                break;
            } else if (
                header.command_fields.noc_read.event != 0xdeadbeef &&
                header.command_fields.noc_read.event > expectedOrderId) {
                throw std::runtime_error(fmt::format(
                    "Read event out of order: {} > {}", header.command_fields.noc_read.event, expectedOrderId));
            }
        };

        HostToLiteFabricReadEvent::increment();
    }

    void send_payload_flush_non_blocking_from_address(
        FabricLiteHeader& header, CoreCoord translated_core_sender, uint32_t channel_address) {
        if (!header.get_payload_size_excluding_header()) {
            return;
        }

        uint32_t addr = get_next_send_buffer_slot_address(channel_address);

        tt_device->write_to_device(&header, translated_core_sender, addr, sizeof(FabricLiteHeader));

        // TODO: Membar shouldn't be need here because we are using TTDevice read/writes which
        // are using strict ordering so it should commit transactions in order they were issued.
        // chip->l1_membar({translated_core_sender});

        h2d.sender_host_write_index =
            lite_fabric::wrap_increment<SENDER_NUM_BUFFERS_ARRAY[0]>(h2d.sender_host_write_index);

        log_debug(LogUMD, "Flushing h2d sender_host_write_index to {}", h2d.sender_host_write_index);
        flush_h2d(translated_core_sender);
    }

    void send_payload_without_header_non_blocking_from_address(
        void* data, size_t size, CoreCoord translated_core_sender, uint32_t channel_address) {
        if (!size) {
            return;
        }
        if (size > CHANNEL_BUFFER_SIZE - sizeof(FabricLiteHeader)) {
            throw std::runtime_error("Payload size exceeds channel buffer size");
        }
        uint32_t addr = get_next_send_buffer_slot_address(channel_address) + sizeof(FabricLiteHeader);
        log_debug(LogUMD, "Send {}B payload only {:#x}", size, addr);
        tt_device->write_to_device(data, translated_core_sender, addr, size);
    }

    void flush_h2d(CoreCoord translated_core_sender) {
        tt_driver_atomics::mfence();

        tt_device->write_to_device(
            (void*)(reinterpret_cast<uintptr_t>(this) + offsetof(HostToLiteFabricInterface, h2d)),
            translated_core_sender,

            host_interface_on_device_addr + offsetof(HostToLiteFabricInterface, h2d),
            sizeof(HostToDevice));

        // TODO: Membar shouldn't be need here because we are using TTDevice read/writes which
        // are using strict ordering so it should commit transactions in order they were issued.
        // chip->l1_membar({translated_core_sender});
    }

    void write_one_page(void* mem_ptr, size_t size, CoreCoord sender_core, uint64_t dst_noc_addr) {
        FabricLiteHeader header;
        header.to_chip_unicast(1);
        header.to_noc_unicast_write(lite_fabric::NocUnicastCommandHeader{dst_noc_addr}, size);

        header.unaligned_offset = dst_noc_addr & (l1_alignment_bytes - 1);

        wait_for_empty_write_slot(sender_core);

        send_payload_without_header_non_blocking_from_address(
            mem_ptr, size, sender_core, sender_channel_base + header.unaligned_offset);
        send_payload_flush_non_blocking_from_address(header, sender_core, sender_channel_base);
    }

    void write_noc_addr(void* mem_ptr, size_t size, CoreCoord sender_core, uint64_t dst_noc_addr) {
        size_t num_pages = size / get_max_payload_data_size_bytes();
        for (size_t i = 0; i < num_pages; i++) {
            write_one_page(
                reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem_ptr) + i * get_max_payload_data_size_bytes()),
                get_max_payload_data_size_bytes(),
                sender_core,
                dst_noc_addr + i * get_max_payload_data_size_bytes());
        }

        size_t remaining_bytes = size % get_max_payload_data_size_bytes();
        if (remaining_bytes > 0) {
            write_one_page(
                reinterpret_cast<void*>(
                    reinterpret_cast<uintptr_t>(mem_ptr) + num_pages * get_max_payload_data_size_bytes()),
                remaining_bytes,
                sender_core,
                dst_noc_addr + num_pages * get_max_payload_data_size_bytes());
        }
    }

    void read_one_page(void* mem_ptr, size_t size, CoreCoord receiver_core, uint64_t src_noc_addr) {
        FabricLiteHeader header;
        header.to_chip_unicast(1);
        header.to_noc_read(lite_fabric::NocReadCommandHeader{src_noc_addr, HostToLiteFabricReadEvent::get()}, size);
        header.unaligned_offset = 0;

        uint32_t receiver_header_address = get_next_receiver_buffer_slot_address(receiver_channel_base);
        log_debug(
            LogUMD,
            "Reading data from {} {:#x} unaligned {}",
            receiver_core.str(),
            receiver_header_address,
            header.unaligned_offset);
        uint32_t receiver_data_address = receiver_header_address + sizeof(FabricLiteHeader);

        wait_for_empty_write_slot(receiver_core);
        send_payload_flush_non_blocking_from_address(header, receiver_core, sender_channel_base);

        wait_for_read_event(receiver_core, receiver_header_address);

        uint8_t read_back_unaligned_offset = 0;
        tt_device->read_from_device(
            &read_back_unaligned_offset,
            receiver_core,

            receiver_header_address + offsetof(FabricLiteHeader, unaligned_offset),
            sizeof(uint8_t));

        tt_device->read_from_device(mem_ptr, receiver_core, receiver_data_address + read_back_unaligned_offset, size);

        h2d.receiver_host_read_index =
            lite_fabric::wrap_increment<RECEIVER_NUM_BUFFERS_ARRAY[0]>(h2d.receiver_host_read_index);
        flush_h2d(receiver_core);
    }

    void read_noc_addr(void* mem_ptr, size_t size, CoreCoord receiver_core, uint64_t src_noc_addr) {
        size_t num_pages = size / get_max_payload_data_size_bytes();
        for (size_t i = 0; i < num_pages; i++) {
            read_one_page(
                reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem_ptr) + i * get_max_payload_data_size_bytes()),
                get_max_payload_data_size_bytes(),
                receiver_core,
                src_noc_addr + i * get_max_payload_data_size_bytes());
        }

        size_t remaining_bytes = size % get_max_payload_data_size_bytes();
        if (remaining_bytes > 0) {
            read_one_page(
                reinterpret_cast<void*>(
                    reinterpret_cast<uintptr_t>(mem_ptr) + num_pages * get_max_payload_data_size_bytes()),
                remaining_bytes,
                receiver_core,
                src_noc_addr + num_pages * get_max_payload_data_size_bytes());
        }
    }
}

__attribute__((packed));

struct LiteFabricMemoryMap {
    lite_fabric::LiteFabricConfig config;
    EDMChannelWorkerLocationInfo sender_location_info;
    uint32_t sender_flow_control_semaphore{};
    unsigned char padding0[12]{};
    uint32_t sender_connection_live_semaphore{};
    unsigned char padding1[12]{};
    uint32_t worker_semaphore{};
    unsigned char padding2[92]{};
    unsigned char sender_channel_buffer[lite_fabric::SENDER_NUM_BUFFERS_ARRAY[0] * lite_fabric::CHANNEL_BUFFER_SIZE]{};
    unsigned char padding3[192]{};
    unsigned char
        receiver_channel_buffer[lite_fabric::RECEIVER_NUM_BUFFERS_ARRAY[0] * lite_fabric::CHANNEL_BUFFER_SIZE]{};
    // L1 address of the service_lite_fabric function.
    uint32_t service_lite_fabric_addr{};
    unsigned char padding4[12]{};
    // Must be last because it has members that are only stored on the host.
    HostToLiteFabricInterface<lite_fabric::SENDER_NUM_BUFFERS_ARRAY[0], lite_fabric::CHANNEL_BUFFER_SIZE>
        host_interface;

    static auto make_host_interface(TTDevice* tt_device) {
        lite_fabric::HostToLiteFabricInterface<SENDER_NUM_BUFFERS_ARRAY[0], CHANNEL_BUFFER_SIZE> host_interface;
        host_interface.host_interface_on_device_addr = lite_fabric::LiteFabricMemoryMap::get_host_interface_addr();
        host_interface.sender_channel_base = lite_fabric::LiteFabricMemoryMap::get_send_channel_addr();
        host_interface.receiver_channel_base = lite_fabric::LiteFabricMemoryMap::get_receiver_channel_addr();

        // TODO: these constants need to be moved to HAL once we have it.
        constexpr uint32_t eth_barrier_addr = 12;
        constexpr uint32_t tensix_barrier_addr = 12;
        constexpr uint32_t l1_alignment_bytes = GLOBAL_ALIGNMENT;
        host_interface.eth_barrier_addr = eth_barrier_addr;
        host_interface.tensix_barrier_addr = tensix_barrier_addr;
        host_interface.l1_alignment_bytes = l1_alignment_bytes;
        host_interface.tt_device = tt_device;

        host_interface.init();
        return host_interface;
    }

    static uint32_t get_address() {
        auto addr = LITE_FABRIC_CONFIG_START;
        return addr;
    }

    static uint32_t get_host_interface_addr() {
        return get_address() + offsetof(lite_fabric::LiteFabricMemoryMap, host_interface);
    }

    static uint32_t get_send_channel_addr() {
        return get_address() + offsetof(lite_fabric::LiteFabricMemoryMap, sender_channel_buffer);
    }

    static uint32_t get_receiver_channel_addr() {
        return get_address() + offsetof(lite_fabric::LiteFabricMemoryMap, receiver_channel_buffer);
    }

    static uint32_t get_service_channel_func_addr() {
        return get_address() + offsetof(lite_fabric::LiteFabricMemoryMap, service_lite_fabric_addr);
    }
};

static_assert(offsetof(LiteFabricMemoryMap, sender_flow_control_semaphore) % 16 == 0);
static_assert(offsetof(LiteFabricMemoryMap, sender_connection_live_semaphore) % 16 == 0);
static_assert(offsetof(LiteFabricMemoryMap, worker_semaphore) % 16 == 0);
static_assert(offsetof(LiteFabricMemoryMap, sender_channel_buffer) % GLOBAL_ALIGNMENT == 0);
static_assert(offsetof(LiteFabricMemoryMap, receiver_channel_buffer) % GLOBAL_ALIGNMENT == 0);
static_assert(offsetof(LiteFabricMemoryMap, host_interface) % 16 == 0);

}  // namespace lite_fabric

}  // namespace tt::umd
