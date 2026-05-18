// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/arc_messenger.hpp"

namespace tt::umd {

class MockArcMessenger : public ArcMessenger {
public:
    MockArcMessenger(TTDevice* tt_device) : ArcMessenger(tt_device) {}

    uint32_t send_message(
        const uint32_t /*msg_code*/,
        std::vector<uint32_t>& /*return_values*/,
        const std::vector<uint32_t>& /*args*/,
        const std::chrono::milliseconds /*timeout_ms*/) override {
        // Return 0 to indicate "success" on every ARC message.
        return 0;
    }
};

ArcMessenger::ArcMessenger(TTDevice* tt_device) : tt_device(tt_device) {}

ArcMessenger::~ArcMessenger() = default;

std::unique_ptr<ArcMessenger> ArcMessenger::create_arc_messenger(TTDevice* tt_device) {
    return std::make_unique<MockArcMessenger>(tt_device);
}

}  // namespace tt::umd
