// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <memory>

#include "umd/device/warm_reset.hpp"

using namespace tt;
using namespace tt::umd;

int main(int argc, char* argv[]) {
    WarmReset::notify_all_listeners_with_handshake(std::chrono::milliseconds(5'0000));
    std::cout << "Finished notification\n";
    return 0;
}
