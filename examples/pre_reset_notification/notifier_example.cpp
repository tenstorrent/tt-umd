// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <memory>
#include <string>

#include "umd/device/warm_reset.hpp"

using namespace tt;
using namespace tt::umd;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [--pre|--post]\n";
        return 1;
    }

    std::string arg = argv[1];

    if (arg == "--pre") {
        WarmReset::notify_all_listeners_with_handshake(std::chrono::milliseconds(5'0000));
        std::cout << "Finished pre-notification\n";
        return 0;
    }
    if (arg == "--post") {
        WarmReset::notify_all_listeners_post_reset();
        std::cout << "Finished post-notification\n";
        return 0;
    }
    std::cerr << "Invalid argument. Use --pre or --post\n";
    return 1;
}
