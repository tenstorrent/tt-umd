/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#define SPDLOG_FMT_EXTERNAL
#include "spdlog/spdlog.h"


#define UMD_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define UMD_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define UMD_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define UMD_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
#define UMD_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)