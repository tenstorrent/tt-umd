/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <filesystem>

namespace fs = std::filesystem;

fs::path GetClusterDescYAML();
