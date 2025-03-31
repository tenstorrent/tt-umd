/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/umd_utils.h"

#include <boost/interprocess/permissions.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>

using namespace boost::interprocess;

std::shared_ptr<named_mutex> initialize_mutex(const std::string& mutex_name, const bool clear_mutex) {
    if (clear_mutex) {
        named_mutex::remove(mutex_name.c_str());
    }
    permissions unrestricted_permissions;
    unrestricted_permissions.set_unrestricted();
    return std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);
}

void clear_mutex(const std::string& mutex_name) { named_mutex::remove(mutex_name.c_str()); }
