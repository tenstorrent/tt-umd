// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>

#include "stub_device_model.hpp"
#include "tt_device.hpp"

using namespace tt::umd;

int main() {
    auto model = std::make_unique<StubDeviceModel>();
    TTDevice device(std::move(model));
    device.init_device();

    uint32_t buf = 0;
    CoreCoord core(1, 1);
    device.read_data(&buf, core, 0x1000, sizeof(buf));
    device.write_data(&buf, core, 0x1000, sizeof(buf));

    device.assert_risc_reset(core, RiscType::ALL);
    device.deassert_risc_reset(core, RiscType::ALL);

    printf("Stub device OK (arch=%d, device_id=%d)\n", (int)device.get_arch(), device.get_communication_device_id());
    return 0;
}
