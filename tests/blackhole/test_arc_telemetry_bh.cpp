// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "umd/device/blackhole_arc_telemetry_reader.h"

using namespace tt::umd;

TEST(BlackholeTelemetry, BasicBlackholeTelemetry) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        std::unique_ptr<blackhole::BlackholeArcTelemetryReader> blackhole_arc_telemetry_reader =
            std::make_unique<blackhole::BlackholeArcTelemetryReader>(tt_device.get());

        uint32_t board_id_high = blackhole_arc_telemetry_reader->read_entry(blackhole::TAG_BOARD_ID_HIGH);
        uint32_t board_id_low = blackhole_arc_telemetry_reader->read_entry(blackhole::TAG_BOARD_ID_LOW);

        const uint64_t board_id = ((uint64_t)board_id_high << 32) | (board_id_low);
        EXPECT_NO_THROW(get_board_type_from_board_id(board_id));
    }
}
