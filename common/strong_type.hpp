#pragma once
// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0


// 
// Basic (limited) support for strong typing. This will only support a strong type mapping to one
// type. This functionality is useful for function argument lists where there may be multiple
// arguments of the same underlying type, and we want to prevent the caller from accidentally mixing
// up the order of the arguments.
//
// ONLY USE THIS FOR TRIVIALLY COPYABLE TYPES!!!
//
// The initial motivating example for adding this here is the driver has different types of sizes:
//  - sometimes it is dealing with a size in words
//  - sometimes it is dealing with a size in bytes
//
//  This will let us disambiguate the two types of sizes
//
// See https://www.fluentcpp.com/2016/12/08/strong-types-for-strong-interfaces/ for more information
template <typename T, typename Parameter>
class StrongType
{
public:
    explicit StrongType(T const& value) : value_(value) {}
    explicit StrongType(T&& value) : value_(std::move(value)) {}

    T& get() { return value_; }
    T const& get() const {return value_; }

    operator T&() { return get(); }
    operator T const&() const { return get(); }
    
private:
    T value_;
};