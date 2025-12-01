/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef TT_UMD_USE_BOOST
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

namespace tt::umd {
namespace asio = boost::asio;
using error_code = boost::system::error_code;
using system_error = boost::system::system_error;
}  // namespace tt::umd

#else
#include <asio.hpp>

namespace tt::umd {
namespace asio = ::asio;
using error_code = std::error_code;
using system_error = std::system_error;
}  // namespace tt::umd

#endif
